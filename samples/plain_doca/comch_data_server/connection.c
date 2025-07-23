#include "comch_data_server.h"
#include <inttypes.h>

void producer_send_completed_callback(
    struct doca_comch_producer_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;

    struct connection_state *conn_state = ctx_user_data.ptr;
    struct doca_task *task = doca_comch_producer_task_send_as_task(send_task);
    struct doca_buf *buffer = (struct doca_buf*) doca_comch_producer_task_send_get_buf(send_task);

    doca_buf_dec_refcount(buffer, NULL);
    doca_task_free(task);

    ++conn_state->completed;
    if(conn_state->offloaded < conn_state->server_state->data.block_count) {
        send_next_data_buffer(conn_state);
    } else if(conn_state->producer != NULL) {
        struct doca_ctx *ctx = doca_comch_producer_as_ctx(conn_state->producer);
        doca_ctx_stop(ctx);
        conn_state->producer = NULL;
    }
}

void producer_send_error_callback(
    struct doca_comch_producer_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    struct connection_state *conn_state = ctx_user_data.ptr;
    struct doca_task *task = doca_comch_producer_task_send_as_task(send_task);
    struct doca_buf *buffer = (struct doca_buf*) doca_comch_producer_task_send_get_buf(send_task);

    doca_error_t status = doca_task_get_status(task);
    LOG_ERROR("error from send task %" PRIu64 ": %s", task_user_data.u64, doca_error_get_descr(status));

    doca_buf_dec_refcount(buffer, NULL);
    doca_task_free(task);

    if(conn_state->producer != NULL) {
        struct doca_ctx *ctx = doca_comch_producer_as_ctx(conn_state->producer);
        doca_ctx_stop(ctx);
        conn_state->producer = NULL;
    }
}

struct doca_comch_producer *open_producer(
    struct doca_comch_connection *connection,
    struct connection_state *conn_state, // not yet completely initialized
    struct doca_pe *engine,
    uint32_t max_send_tasks
) {
    struct doca_comch_producer *producer;
    doca_error_t err;

    err = doca_comch_producer_create(connection, &producer);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to create producer subcontext: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_ctx *ctx = doca_comch_producer_as_ctx(producer);
    err = doca_ctx_set_state_changed_cb(ctx, producer_state_change_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set state change callback: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    union doca_data ctx_user_data = { .ptr = conn_state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set producer user data: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_producer_task_send_set_conf(producer, producer_send_completed_callback, producer_send_error_callback, max_send_tasks);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set send task callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to connect producer to progress engine: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to start producer: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return producer;

failure_cleanup:
    doca_comch_producer_destroy(producer);
failure:
    return NULL;
}

struct connection_state *create_connection_state(
    struct doca_comch_server *server,
    struct doca_comch_connection *connection,
    struct doca_pe *engine
) {
    doca_error_t err;
    struct doca_ctx *ctx = doca_comch_server_as_ctx(server);
    union doca_data ctx_user_data;
    struct server_state *server_state;
    
    err = doca_ctx_get_user_data(ctx, &ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get server user data%s", doca_error_get_descr(err));
        goto failure;
    }
    server_state = ctx_user_data.ptr;

    struct connection_state *state = calloc(1, sizeof(struct connection_state));
    if(state == NULL) {
        LOG_ERROR("unable to allocate memory");
        goto failure;
    }

    struct doca_comch_producer *producer = open_producer(connection, state, engine, 8);
    if(producer == NULL) {
        LOG_ERROR("unable to create producer");
        goto failure_cleanup;
    }

    state->server_state = server_state;
    state->connection = connection;
    state->producer = producer;

    return state;

failure_cleanup:
    free(state);
failure:
    return NULL;
}

void destroy_connection_state(struct connection_state *state) {
    if(state == NULL) {
        return;
    }

    doca_comch_producer_destroy(state->producer);
    free(state);
}

void producer_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) ctx;
    (void) prev_state;

    struct connection_state *connstate = (struct connection_state *) user_data.ptr;

    if(next_state == DOCA_CTX_STATE_RUNNING) {

    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        doca_comch_producer_destroy(connstate->producer);
        connstate->producer = NULL;
    }
}

doca_error_t send_data_extents(
    struct doca_comch_server *server,
    struct doca_comch_connection *connection,
    uint32_t block_count,
    uint32_t block_size
) {
    doca_error_t err;
    struct doca_comch_task_send *send_task;
    struct doca_task *task;

    assert(server != NULL);

    char buf[32]; // always enough for two u32 and a space.
    size_t len = snprintf(buf, sizeof buf, "%" PRIu32 " %" PRIu32, block_count, block_size);
    assert(len < sizeof(buf));

    // send including sentinel char
    err = doca_comch_server_task_send_alloc_init(server, connection, buf, len + 1, &send_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not allocate task: %s", doca_error_get_descr(err));
        goto failure;
    }

    task = doca_comch_task_send_as_task(send_task);
    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not submit task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    return DOCA_SUCCESS;

failure_task:
    doca_task_free(task);
failure:
    return err;
}

doca_error_t send_next_data_buffer(
    struct connection_state *conn_state
) {
    doca_error_t err;
    struct doca_buf *buf;

    struct server_state *server_state = conn_state->server_state;
    uint32_t num = conn_state->offloaded;
    uint32_t offset = num * server_state->data.block_size;
    void *addr = server_state->data.base_ptr + offset;

    err = doca_buf_inventory_buf_get_by_data(server_state->buf_inv, server_state->memory_map, addr, server_state->data.block_size, &buf);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get source buffer: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_comch_producer_task_send *send_task;
    err = doca_comch_producer_task_send_alloc_init(conn_state->producer, buf, NULL, 0, conn_state->remote_consumer_id, &send_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not allocate task: %s", doca_error_get_descr(err));
        goto failure_buf;
    }

    struct doca_task *task = doca_comch_producer_task_send_as_task(send_task);
    union doca_data task_user_data = { .u64 = num };
    doca_task_set_user_data(task, task_user_data);

    do {
        err = doca_task_submit(task);
    } while(err == DOCA_ERROR_AGAIN);

    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not submit task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    ++conn_state->offloaded;

    return DOCA_SUCCESS;

failure_task:
    doca_task_free(task);
failure_buf:
    doca_buf_dec_refcount(buf, NULL);
failure:
    return err;
}
