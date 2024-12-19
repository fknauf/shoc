#include "comch_data_server.h"
#include <inttypes.h>

struct server_state *create_server_state(
    struct doca_comch_server *server,
    struct doca_pe *engine,
    struct doca_dev *dev,
    struct data_descriptor *data,
    uint32_t max_buffers
) {
    struct server_state *state = calloc(1, sizeof(struct server_state));
    if(state == NULL) {
        LOG_ERROR("could not allocate server data");
        return NULL;
    }

    struct doca_mmap *memmap = open_memory_map(data->base_ptr, data->block_count * data->block_size, dev, DOCA_ACCESS_FLAG_PCI_READ_WRITE);
    if(memmap == NULL) {
        LOG_ERROR("could not create memory map");
        goto failure;
    }

    struct doca_buf_inventory *buf_inv = open_buffer_inventory(max_buffers);
    if(buf_inv == NULL) {
        LOG_ERROR("could not open buffer inventory");
        goto failure_mmap;
    }

    state->server = server;
    state->engine = engine;
    state->data = *data;
    state->memory_map = memmap;
    state->buf_inv = buf_inv;

    return state;

failure_mmap:
    doca_mmap_destroy(memmap);
failure:
    free(state);
    return NULL;
}

void destroy_server_state(struct server_state *state) {
    doca_buf_inventory_destroy(state->buf_inv);
    doca_mmap_destroy(state->memory_map);
    free(state);
}

void server_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) user_data;
    (void) ctx;
    (void) prev_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        puts("accepting connections");
    }
}

void connection_callback(
    struct doca_comch_event_connection_status_changed *event,
    struct doca_comch_connection *connection,
    uint8_t change_successful
) {
    (void) event;

    if(!change_successful) {
        LOG_ERROR("unsuccessful connection attempt");
        return;
    }

    doca_error_t err;
    struct doca_comch_server *server = doca_comch_server_get_server_ctx(connection);
    struct doca_ctx *ctx = doca_comch_server_as_ctx(server);
    union doca_data ctx_user_data;

    err = doca_ctx_get_user_data(ctx, &ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to get server state: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct server_state *server_state = ctx_user_data.ptr;

    struct connection_state *state = create_connection_state(server, connection, server_state->engine);
    if(state == NULL) {
        LOG_ERROR("unable to create connection state");
        goto failure;
    }

    union doca_data conn_user_data = { .ptr = state };
    err = doca_comch_connection_set_user_data(connection, conn_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set connection user data: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = send_data_extents(server, connection, state->server_state->data.block_count, state->server_state->data.block_size);
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
        LOG_ERROR("unable to send data extents: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return;

failure_cleanup:
    destroy_connection_state(state);
failure:
    doca_comch_server_disconnect(server, connection);
}

void disconnection_callback(
    struct doca_comch_event_connection_status_changed *event,
    struct doca_comch_connection *connection,
    uint8_t change_successful
) {
    (void) event;

    if(!change_successful) {
        LOG_ERROR("unsuccessful disconnection");
        return;
    }

    union doca_data conn_user_data = doca_comch_connection_get_user_data(connection);
    destroy_connection_state(conn_user_data.ptr);
}

void new_consumer_callback(
    struct doca_comch_event_consumer *event,
    struct doca_comch_connection *connection,
    uint32_t id
) {
    (void) event;

    union doca_data conn_user_data = doca_comch_connection_get_user_data(connection);
    struct connection_state *conn_state = conn_user_data.ptr;

    conn_state->remote_consumer_id = id;

    clock_gettime(CLOCK_REALTIME, &conn_state->start);
    send_next_data_buffer(conn_state);
}

void expired_consumer_callback(
    struct doca_comch_event_consumer *event,
    struct doca_comch_connection *connection,
    uint32_t id
) {
    (void) event;

    union doca_data conn_user_data = doca_comch_connection_get_user_data(connection);
    struct connection_state *conn_state = conn_user_data.ptr;

    if(id != conn_state->remote_consumer_id) {
        LOG_ERROR("unknown remote consumer id: %" PRIu32 "", id);
        return;
    }

    doca_ctx_stop(doca_comch_producer_as_ctx(conn_state->producer));
}

void msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;
    (void) recv_buffer;
    (void) msg_len;
    (void) connection;
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
    LOG_ERROR("failure sending message: %s", doca_error_get_descr(status));
    doca_task_free(doca_comch_task_send_as_task(task));
}

struct doca_dev *open_server_device(char const *pci_addr) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    doca_error_t err;

    err = doca_devinfo_create_list(&dev_list, &nb_devs);

    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get device list: %s", doca_error_get_descr(err));
        return NULL;
    }

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        err = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("could not check device pci address: %s", doca_error_get_descr(err));
            continue;
        }

        if(is_addr_equal && doca_comch_cap_server_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            err = doca_dev_open(dev_list[i], &result);

            if(err == DOCA_SUCCESS) {
                goto cleanup;
            } else {
                LOG_ERROR("could not open device: %s", doca_error_get_descr(err));
            }
        }
    }

    LOG_ERROR("no comch server device found");

cleanup:
    doca_devinfo_destroy_list(dev_list);
    return result;
}

struct doca_dev_rep *open_server_device_representor(struct doca_dev *server_dev, char const *pci_addr) {
    struct doca_dev_rep *result = NULL;
    struct doca_devinfo_rep **rep_list = NULL;
    uint32_t nb_reps;
    doca_error_t err;

    err = doca_devinfo_rep_create_list(server_dev, DOCA_DEVINFO_REP_FILTER_NET, &rep_list, &nb_reps);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create representor list: %s", doca_error_get_descr(err));
        return NULL;
    }

    for(uint32_t i = 0; i < nb_reps; ++i) {
        uint8_t is_addr_equal;

        err = doca_devinfo_rep_is_equal_pci_addr(rep_list[i], pci_addr, &is_addr_equal);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("could not compare pci addresses: %s", doca_error_get_descr(err));
            continue;
        }

        if(!is_addr_equal) {
            continue;
        }

        err = doca_dev_rep_open(rep_list[i], &result);
        if(err == DOCA_SUCCESS) {
            goto cleanup;
        } else {
            LOG_ERROR("could not open representor handle: %s", doca_error_get_descr(err));
        }
    }

    LOG_ERROR("could not find representor matching pci address %s", pci_addr);

cleanup:
    doca_devinfo_rep_destroy_list(rep_list);
    return result;
}

struct doca_comch_server *open_server_context(
    struct doca_pe *engine,
    struct doca_dev *dev,
    struct doca_dev_rep *rep,
    struct server_config *config,
    struct data_descriptor *data
) {
    struct doca_comch_server *server;
    doca_error_t err;

    err = doca_comch_server_create(dev, rep, config->server_name, &server);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create context: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct server_state *state = create_server_state(server, engine, dev, data, config->max_buffers);
    if(state == NULL) {
        LOG_ERROR("could not allocate server data");
        goto failure_server;
    }

    struct doca_ctx *ctx = doca_comch_server_as_ctx(server);
    union doca_data ctx_user_data = { .ptr = state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set server state: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_server_set_max_msg_size(server, config->max_msg_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set max message size: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_server_set_recv_queue_size(server, config->recv_queue_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set receiver queue size: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_set_state_changed_cb(ctx, server_state_change_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set state-changed callback: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_server_event_connection_status_changed_register(server, connection_callback, disconnection_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set connection state callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err =  doca_comch_server_event_consumer_register(server, new_consumer_callback, expired_consumer_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set consumer callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_server_task_send_set_conf(server, send_task_completed_callback, send_task_error_callback, config->num_send_tasks);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set send task callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_server_event_msg_recv_register(server, msg_recv_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set msg_recv callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not connect to progress engine: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not start context: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return server;

failure_cleanup:
    free(state);
failure_server:
    doca_comch_server_destroy(server);
failure:
    return NULL;
}

struct doca_pe *open_progress_engine(int epoll_fd) {
    struct doca_pe *engine;
    doca_error_t err;

    err = doca_pe_create(&engine);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create progress engine: %s", doca_error_get_descr(err));
        return NULL;
    }

    doca_event_handle_t event_handle;
    err = doca_pe_get_notification_handle(engine, &event_handle);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not obtain notification handle: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in) != 0) {
        LOG_ERROR("could not attach to epoll handle: %s", strerror(errno));
        goto failure;
    }

    return engine;

failure:
    doca_pe_destroy(engine);
    return NULL;
}

void serve_datastream(
    char const *dev_pci,
    char const *rep_pci,
    struct server_config *config,
    struct data_descriptor *data    
) {
    doca_error_t err;

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1) {
        LOG_ERROR("could not create epoll file descriptor: %s", strerror(errno));
        goto end;
    }

    struct doca_pe *engine = open_progress_engine(epoll_fd);
    if(engine == NULL) {
        LOG_ERROR("could not obtain progress engine");
        goto cleanup_epoll;
    }

    struct doca_dev *server_dev = open_server_device(dev_pci);
    if(server_dev == NULL) {
        LOG_ERROR("could not obtain server device");
        goto cleanup_pe;
    }

    struct doca_dev_rep *server_rep = open_server_device_representor(server_dev, rep_pci);
    if(server_rep == NULL) {
        LOG_ERROR("could not obtain server device representor");
        goto cleanup_dev;
    }

    struct doca_comch_server *server = open_server_context(engine, server_dev, server_rep, config, data);
    if(server == NULL) {
        LOG_ERROR("could not obtain server context");
        goto cleanup_rep;
    }
    
    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        err = doca_ctx_get_state(doca_comch_server_as_ctx(server), &ctx_state);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("could not obtain context state: %s", doca_error_get_descr(err));
            break;
        }

        if(ctx_state == DOCA_CTX_STATE_IDLE) {
            // regular loop condition
            break;
        }

        doca_pe_request_notification(engine);
        nfd = epoll_wait(epoll_fd, &ep_event, 1, 100);

        if(nfd == -1) {
            LOG_ERROR("epoll_wait failed: %s", strerror(errno));
            goto cleanup_context;
        }

        doca_pe_clear_notification(engine, 0);
        while(doca_pe_progress(engine) > 0) {
            // do nothing; doca_pe_progress calls event handlers
        }
    }

    //double elapsed_us = (state.end.tv_sec - state.start.tv_sec) * 1e6 + (state.end.tv_nsec - state.start.tv_nsec) / 1e3;
    //printf("%f microseconds\n", elapsed_us);

cleanup_context:
    doca_comch_server_destroy(server);
cleanup_rep:
    doca_dev_rep_close(server_rep);
cleanup_dev:
    doca_dev_close(server_dev);
cleanup_pe:
    doca_pe_destroy(engine);
cleanup_epoll:
    close(epoll_fd);
end:
}
