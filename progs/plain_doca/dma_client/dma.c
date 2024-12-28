#include "dma_client.h"

_Bool fetch_next_block(struct dma_state *state) {
    doca_error_t err;

    uint32_t num = state->offloaded;
    size_t offset = num * state->client_state->data->block_size;

    printf("offloading block %" PRIu32 "\n", num);

    struct doca_buf *src;
    err = doca_buf_inventory_buf_get_by_data(
        state->buf_inv,
        state->remote_mmap,
        (uint8_t*) state->remote_base + offset,
        state->client_state->data->block_size,
        &src
    );
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get source buffer: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_buf *dest;
    err = doca_buf_inventory_buf_get_by_addr(
        state->buf_inv,
        state->local_mmap,
        state->client_state->data->base_ptr + offset,
        state->client_state->data->block_size,
        &dest
    );
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get destination buffer: %s", doca_error_get_descr(err));
        goto failure_src;
    }

    struct doca_dma_task_memcpy *copy_task;
    union doca_data task_user_data = { .u64 = num };
    err = doca_dma_task_memcpy_alloc_init(state->dma, src, dest, task_user_data, &copy_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to allocate task: %s", doca_error_get_descr(err));
        goto failure_dest;
    }

    struct doca_task *task = doca_dma_task_memcpy_as_task(copy_task);
    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to submit task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    ++state->offloaded;

    return true;

failure_task:
    doca_task_free(task);
failure_dest:
    doca_buf_dec_refcount(dest, NULL);
failure_src:
    doca_buf_dec_refcount(src, NULL);
failure:
    return false;
}

void dma_state_changed_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) prev_state;
    struct dma_state *state = user_data.ptr;
    struct client_state *client_state = state->client_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        clock_gettime(CLOCK_REALTIME, &state->client_state->start);

        for(uint32_t i = 0; i < client_state->parallelism; ++i) {
            if(!fetch_next_block(state)) {
                LOG_ERROR("failed to fetch block %" PRIu32, i);
                doca_ctx_stop(ctx);
            }
        }
    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        destroy_dma_state(state);
        doca_dma_destroy(state->dma);

        struct doca_ctx *client_ctx = doca_comch_client_as_ctx(client_state->client);
        doca_ctx_stop(client_ctx);
    }
}

void dma_memcpy_completed_callback(
    struct doca_dma_task_memcpy *memcpy_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    struct dma_state *state = ctx_user_data.ptr;
    struct doca_task *task = doca_dma_task_memcpy_as_task(memcpy_task);
    struct doca_buf const *src = doca_dma_task_memcpy_get_src(memcpy_task);
    struct doca_buf *dest = doca_dma_task_memcpy_get_dst(memcpy_task);

    doca_buf_dec_refcount((struct doca_buf*) src, NULL);
    doca_buf_dec_refcount(dest, NULL);
    doca_task_free(task);
    ++state->completed;

    if(state->completed == state->client_state->data->block_count) {
        clock_gettime(CLOCK_REALTIME, &state->client_state->end);
        send_done_message(state->client_state);
        doca_ctx_stop(doca_dma_as_ctx(state->dma));
    } else if(state->offloaded < state->client_state->data->block_count) {
        if(!fetch_next_block(state)) {
            LOG_ERROR("failed to fetch block %" PRIu64, task_user_data.u64);
            doca_ctx_stop(doca_dma_as_ctx(state->dma));
        }
    }
}

void dma_memcpy_error_callback(
    struct doca_dma_task_memcpy *memcpy_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    struct dma_state *state = ctx_user_data.ptr;
    struct doca_task *task = doca_dma_task_memcpy_as_task(memcpy_task);
    struct doca_buf const *src = doca_dma_task_memcpy_get_src(memcpy_task);
    struct doca_buf *dest = doca_dma_task_memcpy_get_dst(memcpy_task);

    doca_error_t status = doca_task_get_status(task);
    doca_buf_dec_refcount((struct doca_buf*) src, NULL);
    doca_buf_dec_refcount(dest, NULL);
    doca_task_free(task);

    LOG_ERROR("memcpy %" PRIu64 " failed: %s", task_user_data.u64, doca_error_get_descr(status));
    doca_ctx_stop(doca_dma_as_ctx(state->dma));
}

struct doca_dma *open_dma_context(
    struct client_state *client_state,
    uint8_t *extents_message,
    uint32_t extents_msglen
) {
    doca_error_t err;
    struct doca_dma *dma;

    err = doca_dma_create(client_state->device, &dma);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to open dma context: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_ctx *ctx = doca_dma_as_ctx(dma);
    err = doca_ctx_set_state_changed_cb(ctx, dma_state_changed_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to configure state-changed callback: %s", doca_error_get_descr(err));
        goto failure_dma;
    }

    err = doca_dma_task_memcpy_set_conf(dma, dma_memcpy_completed_callback, dma_memcpy_error_callback, client_state->parallelism);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to configure memcpy event handlers: %s", doca_error_get_descr(err));
        goto failure_dma;
    }

    err = doca_pe_connect_ctx(client_state->engine, ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to connect dma context to progress engine: %s", doca_error_get_descr(err));
        goto failure_dma;
    }

    struct dma_state *dma_state = attach_dma_state(dma, client_state, extents_message, extents_msglen);
    if(dma_state == NULL) {
       LOG_ERROR("unable to attach DMA state");
       goto failure_dma;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to start DMA context: %s", doca_error_get_descr(err));
        goto failure_state;
    }

    return dma;

failure_state:
    destroy_dma_state(dma_state);
failure_dma:
    doca_dma_destroy(dma);
failure:
    return NULL;
}

struct extents_msg {
    uint32_t block_count;
    uint32_t block_size;
    uint8_t export_desc[];
} __attribute__((packed));

struct dma_state *attach_dma_state(
    struct doca_dma *dma,
    struct client_state *client_state,
    uint8_t *recv_buffer,
    uint32_t recv_len
) {
    doca_error_t err;

    struct extents_msg *extents = (struct extents_msg*) recv_buffer;
    size_t export_desc_len = recv_len - 8;

    struct cache_aligned_data *storage = create_cache_aligned_buffer(extents->block_count, extents->block_size);
    if(storage == NULL) {
        LOG_ERROR("unable to allocate cache-aligned storage");
        goto failure;
    }

    struct dma_state *dma_state = calloc(1, sizeof(struct dma_state));
    if(dma_state == NULL) {
        LOG_ERROR("unablet to allocate dma state");
        goto failure_storage;
    }

    dma_state->dma = dma;
    dma_state->client_state = client_state;
    dma_state->local_mmap = open_memory_map(storage->base_ptr, storage->block_count * storage->block_size, client_state->device, DOCA_ACCESS_FLAG_PCI_READ_WRITE);
    if(dma_state->local_mmap == NULL) {
        LOG_ERROR("could not create local memory mapping");
        goto failure_state;
    }

    err = doca_mmap_create_from_export(NULL, extents->export_desc, export_desc_len, client_state->device, &dma_state->remote_mmap);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create remote memory mapping: %s", doca_error_get_descr(err));
        goto failure_state;
    }

    size_t remote_size;
    err = doca_mmap_get_memrange(dma_state->remote_mmap, (void**) &dma_state->remote_base, &remote_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get remote base address: %s", doca_error_get_descr(err));
        goto failure_state;
    }

    dma_state->buf_inv = open_buffer_inventory(client_state->parallelism * 2);
    if(dma_state->buf_inv == NULL) {
        LOG_ERROR("could not create buffer inventory");
        goto failure_state;
    }

    struct doca_ctx *ctx = doca_dma_as_ctx(dma);
    union doca_data ctx_user_data = { .ptr = dma_state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not attach DMA state: %s", doca_error_get_descr(err));
        goto failure_state;
    }

    client_state->data = storage;
    return dma_state;

failure_state:
    destroy_dma_state(dma_state);
failure_storage:
    free(storage);
failure:
    return NULL;
}

void destroy_dma_state(struct dma_state *dma_state) {
    doca_error_t err;

    // can be called during creation in case of error, so dma_state may be only half-initialized.
    // still preferrable to writing all this twice.
    if(dma_state->buf_inv != NULL) {
        err = doca_buf_inventory_destroy(dma_state->buf_inv);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("unable to destroy buffer inventory: %s", doca_error_get_descr(err));
        }
    }

    if(dma_state->remote_mmap != NULL) {
        err = doca_mmap_destroy(dma_state->remote_mmap);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("unable to destroy remote mmap: %s", doca_error_get_descr(err));
        }
    }

    if(dma_state->local_mmap != NULL) {
        err = doca_mmap_destroy(dma_state->local_mmap);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("unable to destroy local mmap: %s", doca_error_get_descr(err));
        }
    }

    free(dma_state);
}
