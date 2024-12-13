#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <sys/epoll.h>

#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    PARALLEL_OFFLOADS = 4
};

struct region {
    uint8_t *base;
    uint32_t size;
};

struct compression_state {
    void *in;
    void *out;
    size_t block_count;
    size_t block_size;
    size_t offloaded;
    size_t completed;

    struct doca_compress *compress;
    struct doca_mmap *mmap_in;
    struct doca_mmap *mmap_out;
    struct doca_buf_inventory *buf_inv;
    struct region *out_regions;

    struct timespec start;
    struct timespec end;
};

int offload_next(
    struct compression_state *state
) {
    doca_error_t err;

    size_t num = state->offloaded;
    size_t offset = state->block_size * num;

    struct doca_buf *buf_in;
    struct doca_buf *buf_out;

    err = doca_buf_inventory_buf_get_by_data(state->buf_inv, state->mmap_in, state->in + offset, state->block_size, &buf_in);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[offload %zu] could not get input  buffer from inventory: %s\n", num, doca_error_get_descr(err));
        goto failure;
    }

    err = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->mmap_out, state->out + offset, state->block_size, &buf_out);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[offload %zu] could not get output buffer from inventory: %s\n", num, doca_error_get_descr(err));
        goto failure_buf_in;
    }

    union doca_data task_user_data = { .u64 = num };
    struct doca_compress_task_compress_deflate *compress_task;
    err = doca_compress_task_compress_deflate_alloc_init(state->compress, buf_in, buf_out, task_user_data, &compress_task);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[offload %zu] could not allocate task: %s\n", num, doca_error_get_descr(err));
        goto failure_buf_out;
    }

    err = doca_task_submit(doca_compress_task_compress_deflate_as_task(compress_task));
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[offload %zu] could not submit task: %s\n", num, doca_error_get_descr(err));
        goto failure_task;
    }

    ++state->offloaded;

    return DOCA_SUCCESS;

failure_task:
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));
failure_buf_out:
    doca_buf_dec_refcount(buf_out, NULL);
failure_buf_in:
    doca_buf_dec_refcount(buf_in, NULL);
failure:
    return err;
}

void compress_state_changed_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) ctx;
    (void) prev_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct compression_state *state = (struct compression_state *) user_data.ptr;
        clock_gettime(CLOCK_REALTIME, &state->start);

        for(uint32_t i = 0; i < PARALLEL_OFFLOADS && i < state->block_count; ++i) {
            offload_next(state);
        }
    }
}

void compress_completed_callback(
    struct doca_compress_task_compress_deflate *compress_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    size_t num = (size_t) task_user_data.u64;
    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;

    struct doca_buf const *buf_in = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *buf_out = doca_compress_task_compress_deflate_get_dst(compress_task);

    void *out_head;
    size_t out_len;
    doca_buf_get_data(buf_out, &out_head);
    doca_buf_get_data_len(buf_out, &out_len);

    ++state->completed;
    state->out_regions[num].base = out_head;
    state->out_regions[num].size = out_len;

    doca_buf_dec_refcount((struct doca_buf*) buf_in, NULL);
    doca_buf_dec_refcount(buf_out, NULL);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

    if(state->offloaded < state->block_count) { 
        offload_next(state);
    } else if(state->completed == state->block_count) {
        clock_gettime(CLOCK_REALTIME, &state->end);
        doca_ctx_stop(doca_compress_as_ctx(state->compress));
    }
}

void compress_error_callback(
    struct doca_compress_task_compress_deflate *compress_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    doca_error_t err = doca_task_get_status(doca_compress_task_compress_deflate_as_task(compress_task));
    size_t num = (size_t) task_user_data.u64;
    fprintf(stderr, "[error %zu] task failed: %s\n", num, doca_error_get_descr(err));

    struct doca_buf const *buf_in = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *buf_out = doca_compress_task_compress_deflate_get_dst(compress_task);

    doca_buf_dec_refcount((struct doca_buf*) buf_in, NULL);
    doca_buf_dec_refcount(buf_out, NULL);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;
    doca_ctx_stop(doca_compress_as_ctx(state->compress));
}

struct doca_dev *open_compress_device(void) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    doca_error_t err;

    err = doca_devinfo_create_list(&dev_list, &nb_devs);

    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open dev] could not get device list: %s\n", doca_error_get_descr(err));
        return NULL;
    }

    for(uint32_t i = 0; i < nb_devs; ++i) {
        if(doca_compress_cap_task_compress_deflate_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            err = doca_dev_open(dev_list[i], &result);

            if(err == DOCA_SUCCESS) {
                goto cleanup;
            } else {
                fprintf(stderr, "[open dev] could not open device: %s\n", doca_error_get_descr(err));
            }
        }
    }

    fprintf(stderr, "[open dev] no compression device found\n");

cleanup:
    doca_devinfo_destroy_list(dev_list);
    return result;
}

struct doca_compress *open_compress_context(
    struct doca_dev *dev,
    struct doca_pe *engine,
    struct compression_state *state
) {
    struct doca_compress *compress;
    struct doca_ctx *ctx;
    doca_error_t err;

    err = doca_compress_create(dev, &compress);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not create context: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    state->compress = compress;

    ctx = doca_compress_as_ctx(compress);
    err = doca_ctx_set_state_changed_cb(ctx, compress_state_changed_callback);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set state-change callback: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    union doca_data ctx_user_data = { .ptr = state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set context user data: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_compress_task_compress_deflate_set_conf(compress, compress_completed_callback, compress_error_callback, state->block_count);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set task callbacks: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not connect to progress engine: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not start context: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return compress;

failure_cleanup:
    doca_compress_destroy(compress);
failure:
    return NULL;
}

struct doca_pe *open_progress_engine(int epoll_fd) {
    struct doca_pe *engine;
    doca_error_t err;

    err = doca_pe_create(&engine);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open pe] could not create progress engine: %s\n", doca_error_get_descr(err));
        return NULL;
    }

    doca_event_handle_t event_handle;
    err = doca_pe_get_notification_handle(engine, &event_handle);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open pe] could not obtain notification handle: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in) != 0) {
        fprintf(stderr, "[open pe] could not attach to epoll handle: %s\n", strerror(errno));
        goto failure;
    }

    return engine;

failure:
    doca_pe_destroy(engine);
    return NULL;
}

struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions) {
    struct doca_mmap *map;
    doca_error_t err;

    err = doca_mmap_create(&map);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open mmap] could not create memory map: %s\n", doca_error_get_descr(err));
        return NULL;
    }

    err = doca_mmap_set_memrange(map, base, size);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open mmap] could not set memory range: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_set_permissions(map, permissions);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open mmap] could not set permissions: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_add_dev(map, dev);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open mmap] could not add device: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_start(map);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open mmap] could not start memory map: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    return map;

failure:
    doca_mmap_destroy(map);
    return NULL;
}

struct region *compress_buffers(
    void *in,
    void *out,
    size_t block_count,
    size_t block_size
) {
    doca_error_t err;

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1) {
        fprintf(stderr, "[compress buffers] could not create epoll file descriptor: %s\n", strerror(errno));
        return NULL;
    }

    struct doca_pe *engine = open_progress_engine(epoll_fd);
    if(engine == NULL) {
        fprintf(stderr, "[compress buffers] could not obtain progress engine\n");
        goto cleanup_epoll;
    }

    struct doca_dev *compress_dev = open_compress_device();
    if(compress_dev == NULL) {
        fprintf(stderr, "[compress buffers] could not obtain compression device\n");
        goto cleanup_pe;
    }

    struct doca_mmap *mmap_in = open_memory_map(in, block_count * block_size, compress_dev, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if(mmap_in == NULL) {
        fprintf(stderr, "[compress buffers] could not obtain input memory map\n");
        goto cleanup_dev;
    }

    struct doca_mmap *mmap_out = open_memory_map(out, block_count * block_size, compress_dev, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if(mmap_in == NULL) {
        fprintf(stderr, "[compress buffers] could not obtain output memory map\n");
        goto cleanup_mmap_in;
    }

    struct doca_buf_inventory *inv;
    err = doca_buf_inventory_create(block_count * 2, &inv);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[compress buffers] could not create buffer inventory: %s\n", doca_error_get_descr(err));
        goto cleanup_mmap_out;
    }

    err = doca_buf_inventory_start(inv);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[compress buffers] could not start buffer inventory: %s\n", doca_error_get_descr(err));
        goto cleanup_inv;
    }

    struct region *out_region_buffer = calloc(block_count, sizeof(struct region));
    struct compression_state state = {
        .in = in,
        .out = out,
        .block_count = block_count,
        .block_size = block_size,
        .offloaded = 0,
        .completed = 0,

        .mmap_in = mmap_in,
        .mmap_out = mmap_out,
        .buf_inv = inv,
        .out_regions = out_region_buffer
    };

    struct doca_compress *compress = open_compress_context(compress_dev, engine, &state);
    if(compress == NULL) {
        fprintf(stderr, "[compress buffers] obtain compression context\n");
        goto cleanup_dev;
    }
    
    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        err = doca_ctx_get_state(doca_compress_as_ctx(compress), &ctx_state);
        if(err != DOCA_SUCCESS) {
            fprintf(stderr, "[compress buffers] could not obtain context state: %s\n", doca_error_get_descr(err));
            break;
        }

        if(ctx_state == DOCA_CTX_STATE_IDLE) {
            // regular loop condition
            break;
        }

        doca_pe_request_notification(engine);
        nfd = epoll_wait(epoll_fd, &ep_event, 1, 100);

        if(nfd == -1) {
            fprintf(stderr, "[compress buffers] epoll_wait failed: %s\n", strerror(errno));
            goto cleanup_context;
        }

        doca_pe_clear_notification(engine, 0);
        while(doca_pe_progress(engine) > 0) {
            // do nothing; doca_pe_progress calls event handlers
        }
    }

    double elapsed_us = (state.end.tv_sec - state.start.tv_sec) * 1e6 + (state.end.tv_nsec - state.start.tv_nsec) / 1e3;
    printf("%f microseconds\n", elapsed_us);

cleanup_context:
    doca_compress_destroy(compress);
cleanup_inv:
    doca_buf_inventory_destroy(inv);
cleanup_mmap_out:
    doca_mmap_destroy(mmap_out);
cleanup_mmap_in:
    doca_mmap_destroy(mmap_in);
cleanup_dev:
    doca_dev_close(compress_dev);
cleanup_pe:
    doca_pe_destroy(engine);
cleanup_epoll:
    close(epoll_fd);

    return out_region_buffer;
}

void compress_file(FILE *in, FILE *out) {
    uint32_t batches;
    uint32_t batchsize;

    if(
        fread(&batches, sizeof batches, 1, in) != 1 ||
        fread(&batchsize, sizeof batchsize, 1, in) != 1
    ) {
        fprintf(stderr, "[compress file] could not read data dimensions from input file\n");
        return;
    }

    uint8_t *indata = malloc(batches * batchsize);
    uint8_t *outdata = malloc(batches * batchsize);

    if(indata == NULL || outdata == NULL) {
        fprintf(stderr, "[compress file] could not allocate memory\n");
        goto cleanup;
    }

    if(fread(indata, batchsize, batches, in) != batches) {
        fprintf(stderr, "[compress file] could not read data from input file\n");
        goto cleanup;
    }

    struct region *out_regions = compress_buffers(indata, outdata, batches, batchsize);

    if(out_regions == NULL) {
        fprintf(stderr, "[compress file] buffer compression failed\n");
        goto cleanup;
    }

    fwrite(&batches, sizeof batches, 1, out);
    fwrite(&batchsize, sizeof batchsize, 1, out);

    for(uint32_t i = 0; i < batches; ++i) {
        fwrite(&out_regions[i].size, sizeof out_regions[i].size, 1, out);
        fwrite(out_regions[i].base, 1, out_regions[i].size, out);
    }

    free(out_regions);
cleanup:
    free(indata);
    free(outdata);
}

int main(int argc, char *argv[]) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    if(argc < 3) {
        fprintf(stderr, "Usage: %s INFILE OUTFILE\n", argv[0]);
        return -1;
    }

    FILE *in = fopen(argv[1], "rb");
    if(in == NULL) {
        fprintf(stderr, "[main] failed to open %s: %s\n", argv[1], strerror(errno));
        goto end;
    }

    FILE *out = fopen(argv[2], "wb");    
    if(out == NULL) {
        fprintf(stderr, "[main] failed to open %s: %s\n", argv[2], strerror(errno));
        goto cleanup_in;
    }

    compress_file(in, out);

    fclose(out);
cleanup_in:
    fclose(in);
end:
}
