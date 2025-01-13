#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <sys/epoll.h>

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct client_config {
    char const *server_name;
    uint32_t num_send_tasks;
    uint32_t max_msg_size;
    uint32_t recv_queue_size;
};

struct client_state {
    struct doca_comch_client *client;

    struct timespec start;
    struct timespec end;
};

void send_ping(struct doca_comch_client *client) {
    doca_error_t err;
    struct doca_comch_task_send *send_task;
    struct doca_task *task;
    struct doca_comch_connection *connection;

    assert(client != NULL);

    err = doca_comch_client_get_connection(client, &connection);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[send ping] could not get client connection: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_comch_client_task_send_alloc_init(client, connection, "ping", 4, &send_task);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[send ping] could not allocate task: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    task = doca_comch_task_send_as_task(send_task);
    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[send ping] could not submit task: %s\n", doca_error_get_descr(err));
        goto failure_cleanup_task;
    }

    return;

failure_cleanup_task:
    doca_task_free(task);
failure:
}

void state_changed_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) ctx;
    (void) prev_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct client_state *state = (struct client_state *) user_data.ptr;
        clock_gettime(CLOCK_MONOTONIC, &state->start);
        send_ping(state->client);
    }
}

void msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;

    doca_error_t err;
    struct doca_comch_client *client = doca_comch_client_get_client_ctx(connection);
    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    union doca_data ctx_user_data;

    err = doca_ctx_get_user_data(ctx, &ctx_user_data);
    if(err != DOCA_SUCCESS || ctx_user_data.ptr == NULL) {
        fprintf(stderr, "[msg recv callback] failed to get user data: %s\n", doca_error_get_descr(err));
    }

    struct client_state *state = (struct client_state*) ctx_user_data.ptr;
    clock_gettime(CLOCK_MONOTONIC, &state->end);

    fwrite(recv_buffer, 1, msg_len, stdout);
    puts("");

    err = doca_ctx_stop(ctx);
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
        fprintf(stderr, "[msg recv callback] failed to stop client: %s\n", doca_error_get_descr(err));
    }
}

void send_task_completed_callback(
    struct doca_comch_task_send *task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    doca_task_free(doca_comch_task_send_as_task(task));
}

void send_task_error_callback(
    struct doca_comch_task_send *task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    doca_error_t status = doca_task_get_status(doca_comch_task_send_as_task(task));
    fprintf(stderr, "[send error] failure sending message: %s\n", doca_error_get_descr(status));
    doca_task_free(doca_comch_task_send_as_task(task));
}

struct doca_dev *open_client_device(char const *pci_addr) {
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
        uint8_t is_addr_equal = 0;

        err = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
        if(err != DOCA_SUCCESS) {
            fprintf(stderr, "[open dev] could not check device pci address: %s\n", doca_error_get_descr(err));
            continue;
        }

        if(is_addr_equal && doca_comch_cap_client_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            err = doca_dev_open(dev_list[i], &result);

            if(err == DOCA_SUCCESS) {
                goto cleanup;
            } else {
                fprintf(stderr, "[open dev] could not open device: %s\n", doca_error_get_descr(err));
            }
        }
    }

    fprintf(stderr, "[open dev] no comch client device found\n");

cleanup:
    doca_devinfo_destroy_list(dev_list);
    return result;
}

struct doca_comch_client *open_client_context(
    struct doca_dev *dev,
    struct client_config *config,
    struct doca_pe *engine,
    struct client_state *state
) {
    struct doca_comch_client *client;
    doca_error_t err;

    err = doca_comch_client_create(dev, config->server_name, &client);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not create context: %s\n", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_comch_client_set_max_msg_size(client, config->max_msg_size);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set max message size: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_set_recv_queue_size(client, config->recv_queue_size);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set receiver queue size: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    union doca_data ctx_user_data = { .ptr = state };
    state->client = client;
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] failed to set ctx user data:%s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_set_state_changed_cb(ctx, state_changed_callback);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set state-changed callback: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_task_send_set_conf(client, send_task_completed_callback, send_task_error_callback, config->num_send_tasks);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set send task callbacks: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_event_msg_recv_register(client, msg_recv_callback);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not set msg_recv callbacks: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        fprintf(stderr, "[open context] could not connect to progress engine: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
        fprintf(stderr, "[open context] could not start context: %s\n", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return client;

failure_cleanup:
    doca_comch_client_destroy(client);
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

void client_ping_pong(
    char const *dev_pci,
    struct client_config *config
) {
    doca_error_t err;

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1) {
        fprintf(stderr, "[serve] could not create epoll file descriptor: %s\n", strerror(errno));
        goto end;
    }

    struct doca_pe *engine = open_progress_engine(epoll_fd);
    if(engine == NULL) {
        fprintf(stderr, "[serve] could not obtain progress engine\n");
        goto cleanup_epoll;
    }

    struct doca_dev *client_dev = open_client_device(dev_pci);
    if(client_dev == NULL) {
        fprintf(stderr, "[serve] could not obtain client device\n");
        goto cleanup_pe;
    }

    struct client_state state;
    struct doca_comch_client *client = open_client_context(client_dev, config, engine, &state);
    if(client == NULL) {
        fprintf(stderr, "[serve] could not obtain client context\n");
        goto cleanup_dev;
    }
    
    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        err = doca_ctx_get_state(doca_comch_client_as_ctx(client), &ctx_state);
        if(err != DOCA_SUCCESS) {
            fprintf(stderr, "[serve] could not obtain context state: %s\n", doca_error_get_descr(err));
            break;
        }

        if(ctx_state == DOCA_CTX_STATE_IDLE) {
            // regular loop condition
            break;
        }

        doca_pe_request_notification(engine);
        nfd = epoll_wait(epoll_fd, &ep_event, 1, 100);

        if(nfd == -1) {
            fprintf(stderr, "[serve] epoll_wait failed: %s\n", strerror(errno));
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
    doca_comch_client_destroy(client);
cleanup_dev:
    doca_dev_close(client_dev);
cleanup_pe:
    doca_pe_destroy(engine);
cleanup_epoll:
    close(epoll_fd);
end:
}

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    struct client_config config = {
        .server_name = "shoc-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16
    };

    client_ping_pong("81:00.0", &config);
}
