#include "dma_client.h"

void send_done_message(struct client_state *state) {
    doca_error_t err;
    struct doca_comch_task_send *send_task;
    struct doca_comch_connection *connection;

    err = doca_comch_client_get_connection(state->client, &connection);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get connection from client: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_comch_client_task_send_alloc_init(state->client, connection, "done", 4, &send_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to allocate send task: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_task *task = doca_comch_task_send_as_task(send_task);
    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to submit send task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    return;

failure_task:
    doca_task_free(task);
failure:
}

void client_state_changed_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) user_data;
    (void) ctx;
    (void) prev_state;
    (void) next_state;
}

void client_send_completed_callback(
    struct doca_comch_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;

    struct client_state *state = ctx_user_data.ptr;
    struct doca_ctx *ctx = doca_comch_client_as_ctx(state->client);

    doca_task_free(doca_comch_task_send_as_task(send_task));
    doca_ctx_stop(ctx);
}

void client_send_error_callback(
    struct doca_comch_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    struct client_state *state = ctx_user_data.ptr;
    struct doca_task *task = doca_comch_task_send_as_task(send_task);
    doca_error_t err = doca_task_get_status(task);

    LOG_ERROR("unable to send message %" PRIu64 ": %s", task_user_data.u64, doca_error_get_descr(err));

    doca_task_free(task);
    doca_ctx_stop(doca_comch_client_as_ctx(state->client));
}

void client_msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;

    doca_error_t err;

    struct doca_comch_client *client = doca_comch_client_get_client_ctx(connection);
    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    union doca_data client_user_data;

    err = doca_ctx_get_user_data(ctx, &client_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get client state from client");
        doca_ctx_stop(ctx);
        return;
    }

    struct client_state *state = client_user_data.ptr;
    struct doca_dma *dma = open_dma_context(state, recv_buffer, msg_len);

    if(dma == NULL) {
        LOG_ERROR("unable to start DMA context");
        doca_ctx_stop(ctx);
        return;
    }
}

struct doca_comch_client *open_client_context(
    struct doca_pe *engine,
    struct doca_dev *dev,
    struct client_config *config,
    struct client_state *state
) {
    struct doca_comch_client *client;
    doca_error_t err;

    err = doca_comch_client_create(dev, config->server_name, &client);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create context: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_comch_client_set_max_msg_size(client, config->max_msg_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set max message size: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_set_recv_queue_size(client, config->recv_queue_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set receiver queue size: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    union doca_data ctx_user_data = { .ptr = state };
    state->client = client;
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("failed to set ctx user data:%s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_set_state_changed_cb(ctx, client_state_changed_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set state-changed callback: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_task_send_set_conf(client, client_send_completed_callback, client_send_error_callback, config->num_send_tasks);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set send task callbacks: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_event_msg_recv_register(client, client_msg_recv_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set msg_recv callbacks: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not connect to progress engine: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
        LOG_ERROR("could not start context: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return client;

failure_cleanup:
    doca_comch_client_destroy(client);
failure:
    return NULL;
}
