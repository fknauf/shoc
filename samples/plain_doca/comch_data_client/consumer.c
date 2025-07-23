#include "comch_data_client.h"

void receive_next_block(struct consumer_state *state) {
    struct client_state *client_state = state->client_state;
    struct cache_aligned_storage *storage = client_state->result;

    if(state->offloaded == storage->block_count) {
        return;
    }

    doca_error_t err;
    uint32_t block_num = state->offloaded;
    void *block_base = cache_aligned_storage_block(storage, block_num);

    struct doca_buf *dest;
    err = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->memory_map, block_base, storage->block_size, &dest);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get destination buffer: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_comch_consumer_task_post_recv *recv_task;
    err = doca_comch_consumer_task_post_recv_alloc_init(state->consumer, dest, &recv_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create post_recv task: %s", doca_error_get_descr(err));
        goto failure_buf;
    }

    struct doca_task *task = doca_comch_consumer_task_post_recv_as_task(recv_task);
    union doca_data task_user_data = { .u64 = block_num };
    doca_task_set_user_data(task, task_user_data);

    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not submit task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    ++state->offloaded;

    return;

failure_task:
    doca_task_free(task);
failure_buf:
    doca_buf_dec_refcount(dest, NULL);
failure:
}

void consumer_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) ctx;
    (void) prev_state;

    struct consumer_state *state = user_data.ptr;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        clock_gettime(CLOCK_MONOTONIC, &state->client_state->start);
        receive_next_block(state);
    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        doca_comch_consumer_destroy(state->consumer);
        state->consumer = NULL;
        doca_ctx_stop(doca_comch_client_as_ctx(state->client_state->client));
        destroy_consumer_state(state);
    }
}

void consumer_recv_completed_callback(
    struct doca_comch_consumer_task_post_recv *recv_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    struct doca_task *task = doca_comch_consumer_task_post_recv_as_task(recv_task);
    struct doca_buf *buf = doca_comch_consumer_task_post_recv_get_buf(recv_task);
    struct consumer_state *state = ctx_user_data.ptr;

    doca_buf_dec_refcount(buf, NULL);
    doca_task_free(task);
    receive_next_block(state);

    ++state->completed;
    if(state->completed == state->client_state->result->block_count) {
        clock_gettime(CLOCK_MONOTONIC, &state->client_state->end);
        doca_ctx_stop(doca_comch_consumer_as_ctx(state->consumer));
    }
}

void consumer_recv_error_callback(
    struct doca_comch_consumer_task_post_recv *recv_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    struct doca_task *task = doca_comch_consumer_task_post_recv_as_task(recv_task);
    struct doca_buf *buf = doca_comch_consumer_task_post_recv_get_buf(recv_task);
    struct consumer_state *state = ctx_user_data.ptr;
    doca_error_t status = doca_task_get_status(task);

    LOG_ERROR("post_recv %" PRIu64 " failed: %s", task_user_data.u64, doca_error_get_descr(status));
    doca_buf_dec_refcount(buf, NULL);
    doca_task_free(task);
    doca_ctx_stop(doca_comch_consumer_as_ctx(state->consumer));
}

void spawn_consumer(
    struct doca_comch_connection *connection,
    uint32_t block_count,
    uint32_t block_size
) {
    doca_error_t err;

    struct doca_comch_client *client = doca_comch_client_get_client_ctx(connection);
    if(client == NULL) {
        LOG_ERROR("no client attached to connection");
        goto failure;
    }

    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    union doca_data ctx_user_data;
    err = doca_ctx_get_user_data(ctx, &ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get client user data: %s", doca_error_get_descr(err));
        goto failure;
    }
    struct client_state *client_state = ctx_user_data.ptr;

    struct cache_aligned_storage *storage = create_cache_aligned_storage(block_count, block_size);
    if(storage == NULL) {
        LOG_ERROR("could not allocate storage");
        goto failure;
    }

    struct consumer_state *consumer_state = create_consumer_state(client_state, storage);
    if(consumer_state == NULL) {
        LOG_ERROR("Could not allocate consumer state");
        goto failure_storage;
    }

    struct doca_comch_consumer *consumer = open_consumer(connection, consumer_state);
    if(consumer == NULL) {
        LOG_ERROR("unable to open consumer");
        goto failure_consumer_state;
    }

    client_state->result = storage;
    return;

failure_consumer_state:
    destroy_consumer_state(consumer_state);
failure_storage:
    destroy_cache_aligned_storage(storage);
failure:
}

struct consumer_state *create_consumer_state(
    struct client_state *client_state,
    struct cache_aligned_storage *storage
) {
    struct consumer_state *state = calloc(1, sizeof(struct consumer_state));
    if(state == NULL) {
        LOG_ERROR("unable to allocate consumer state");
        goto failure;
    }

    uint32_t memmap_size = storage->block_count * storage->block_size;
    struct doca_mmap *memmap = open_memory_map(storage->bytes, memmap_size, client_state->device, DOCA_ACCESS_FLAG_PCI_READ_WRITE);
    if(memmap == NULL) {
        LOG_ERROR("unable to open memory map");
        goto failure_state;
    }

    struct doca_buf_inventory *bufinv = open_buffer_inventory(1);
    if(bufinv == NULL) {
        LOG_ERROR("unable to open buffer inventory");
        goto failure_mmap;
    }

    state->client_state = client_state;
    state->client_state->result = storage;
    state->memory_map = memmap;
    state->buf_inv = bufinv;

    return state;

failure_mmap:
    doca_mmap_destroy(memmap);
failure_state:
    free(state);
failure:
    return NULL;
}

void destroy_consumer_state(struct consumer_state *state) {
    doca_buf_inventory_destroy(state->buf_inv);
    doca_mmap_destroy(state->memory_map);
    free(state);
}

struct doca_comch_consumer *open_consumer(
    struct doca_comch_connection *connection,
    struct consumer_state *state
) {
    doca_error_t err;

    struct doca_comch_consumer *consumer;
    err = doca_comch_consumer_create(connection, state->memory_map, &consumer);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to create consumer: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_ctx *ctx = doca_comch_consumer_as_ctx(consumer);
    union doca_data ctx_user_data = { .ptr = state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set consumer user data: %s", doca_error_get_descr(err));
        goto failure_consumer;
    }

    err = doca_ctx_set_state_changed_cb(ctx, consumer_state_change_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to configure consumer state-change callback: %s", doca_error_get_descr(err));
        goto failure_consumer;
    }

    err = doca_comch_consumer_task_post_recv_set_conf(consumer, consumer_recv_completed_callback, consumer_recv_error_callback, 1);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to configure consumer post_recv callbacks: %s", doca_error_get_descr(err));
        goto failure_consumer;
    }

    err = doca_pe_connect_ctx(state->client_state->engine, ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to connect consumer to progress engine: %s", doca_error_get_descr(err));
        goto failure_consumer;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
        LOG_ERROR("unable to start consumer: %s\n", doca_error_get_descr(err));
        goto failure_consumer;
    }

    state->consumer = consumer;
    return consumer;

failure_consumer:
    doca_comch_consumer_destroy(consumer);
failure:
    return NULL;
}
