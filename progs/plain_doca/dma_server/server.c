#include "dma_server.h"

#include <sys/epoll.h>
#include <unistd.h>

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

    struct doca_comch_server *server = doca_comch_server_get_server_ctx(connection);
    if(server == NULL) {
        LOG_ERROR("could not get server from connection");
        goto failure;
    }
    struct doca_ctx *ctx = doca_comch_server_as_ctx(server);

    union doca_data ctx_user_data;
    doca_error_t err = doca_ctx_get_user_data(ctx, &ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get ctx user data: %s", doca_error_get_descr(err));
        goto failure_server;
    }
    struct cache_aligned_data *data = ctx_user_data.ptr;

    struct doca_dev *dev;
    err = doca_comch_server_get_device(server, &dev);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not get device from server: %s", doca_error_get_descr(err));
        goto failure_server;
    }

    struct doca_mmap *memmap = open_memory_map(data->base_ptr, data->block_count * data->block_size, dev, DOCA_ACCESS_FLAG_PCI_READ_ONLY);
    if(memmap == NULL) {
        LOG_ERROR("could not create memory mapping: %s", doca_error_get_descr(err));
        goto failure_server;
    }

    void const *export_desc;
    size_t export_desc_len;
    err = doca_mmap_export_pci(memmap, dev, &export_desc, &export_desc_len);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to export memory mapping: %s", doca_error_get_descr(err));
        goto failure_mmap;
    }

    struct extents_msg *msg = create_extents_msg(data, export_desc, export_desc_len);
    if(msg == NULL) {
        LOG_ERROR("unable to allocate extents message");
        goto failure_mmap;
    }

    struct connection_state *connstate = create_connection_state(connection, memmap);
    if(connstate == NULL) {
        LOG_ERROR("unable to allocate connection state");
        goto failure_msg;
    }

    union doca_data conn_user_data = { .ptr = connstate };
    err = doca_comch_connection_set_user_data(connection, conn_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to set connection user data: %s", doca_error_get_descr(err));
        goto failure_connstate;
    }

    struct doca_comch_task_send *send_task;
    err = doca_comch_server_task_send_alloc_init(server, connection, msg->bytes, msg->length, &send_task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to allocate task: %s", doca_error_get_descr(err));
        goto failure_connstate;
    }

    struct doca_task *task = doca_comch_task_send_as_task(send_task);
    doca_task_set_user_data(task, conn_user_data);

    err = doca_task_submit(task);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to submit task: %s", doca_error_get_descr(err));
        goto failure_task;
    }

    free(msg);
    return;

failure_task:
    doca_task_free(task);
failure_connstate:
    free(connstate);
failure_msg:
    free(msg);
failure_mmap:
    doca_mmap_destroy(memmap);
failure_server:
    doca_comch_server_disconnect(server, connection);
failure:
}

void disconnection_callback(
    struct doca_comch_event_connection_status_changed *event,
    struct doca_comch_connection *connection,
    uint8_t change_successful
) {
    (void) event;

    if(!change_successful) {
        LOG_ERROR("unsucessful disconnection");
        return;
    }

    union doca_data conn_user_data = doca_comch_connection_get_user_data(connection);

    if(conn_user_data.ptr == NULL) {
        // disconnected before connection state was set
        return;
    }

    struct connection_state *connstate = conn_user_data.ptr;
    destroy_connection_state(connstate, true);
}

void send_task_completed_callback(
    struct doca_comch_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    struct doca_task *task = doca_comch_task_send_as_task(send_task);
    doca_task_free(task);
}

void send_task_error_callback(
    struct doca_comch_task_send *send_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) ctx_user_data;

    struct doca_task *task = doca_comch_task_send_as_task(send_task);
    doca_error_t status = doca_task_get_status(task);
    doca_task_free(task);

    LOG_ERROR("failed to send message: %s", doca_error_get_descr(status));

    struct connection_state *connstate = task_user_data.ptr;
    destroy_connection_state(connstate, false);
}

void msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;

    if(msg_len != 4 || memcmp(recv_buffer, "done", 4) != 0) {
        LOG_ERROR("unexpected message");
        return;
    }

    union doca_data conn_user_data = doca_comch_connection_get_user_data(connection);
    if(conn_user_data.ptr == NULL) {
        LOG_ERROR("message received before we even started?");
        return;
    }
}

struct extents_msg *create_extents_msg(
    struct cache_aligned_data *data,
    void const *export_desc,
    size_t export_desc_len
) {
    size_t msglen = export_desc_len + 8;
    struct extents_msg *message = malloc(sizeof(struct extents_msg) + msglen);

    if(message != NULL) {
        message->length = msglen;
        memcpy(message->bytes, &data->block_count, 4);
        memcpy(message->bytes + 4, &data->block_size, 4);
        memcpy(message->bytes + 8, export_desc, export_desc_len);
    }

    return message;
}

struct connection_state *create_connection_state(
    struct doca_comch_connection *connection,
    struct doca_mmap *memmap
) {
    struct connection_state *state = malloc(sizeof(struct connection_state));

    if(state != NULL) {
        state->connection = connection;
        state->memmap = memmap;
    }

    return state;
}

void destroy_connection_state(struct connection_state *state, _Bool already_disconnected) {
    if(state == NULL) {
        return;
    }

    doca_error_t err;

    err = doca_mmap_destroy(state->memmap);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("unable to destroy memory mapping: %s", doca_error_get_descr(err));
    }

    if(!already_disconnected) {
        struct doca_comch_server *server = doca_comch_server_get_server_ctx(state->connection);
        assert(server != NULL);

        err = doca_comch_server_disconnect(server, state->connection);
        if(err != DOCA_SUCCESS) {
            LOG_ERROR("unable to disconnect: %s", doca_error_get_descr(err));
        }
    }

    free(state);
}

void serve_dma(struct server_config *server_config, struct cache_aligned_data *data) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1) {
        LOG_ERROR("could not create epoll file descriptor: %s", strerror(errno));
        goto failure;
    }

    struct doca_pe *engine = open_progress_engine(epoll_fd);
    if(engine == NULL) {
        LOG_ERROR("could not obtain progress engine");
        goto failure_epoll;
    }

    struct doca_dev *dev = open_device(server_config->dev_pci);
    if(dev == NULL) {
        LOG_ERROR("unable to open device");
        goto failure_engine;
    }

    struct doca_dev_rep *rep = open_device_representor(dev, server_config->dev_rep_pci);
    if(rep == NULL) {
        LOG_ERROR("unable to open representor");
        goto failure_dev;
    }

    struct doca_comch_server *server = open_server_context(engine, dev, rep, server_config, data);
    if(server == NULL) {
        LOG_ERROR("unable to open server context");
        goto failure_rep;
    }

    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        doca_error_t err = doca_ctx_get_state(doca_comch_server_as_ctx(server), &ctx_state);
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
            goto failure_context;
        }

        doca_pe_clear_notification(engine, 0);
        while(doca_pe_progress(engine) > 0) {
            // do nothing; doca_pe_progress calls event handlers
        }
    }

failure_context:
    doca_comch_server_destroy(server);
failure_rep:
    doca_dev_rep_close(rep);
failure_dev:
    doca_dev_close(dev);
failure_engine:
    doca_pe_destroy(engine);
failure_epoll:
    close(epoll_fd);
failure:
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

struct doca_dev *open_device(char const *pci_addr) {
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

        if(is_addr_equal
            && doca_comch_cap_server_is_supported(dev_list[i]) == DOCA_SUCCESS
            && doca_dma_cap_task_memcpy_is_supported(dev_list[i]) == DOCA_SUCCESS
        ) {
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

struct doca_dev_rep *open_device_representor(struct doca_dev *dev, char const *pci_addr) {
    struct doca_dev_rep *result = NULL;
    struct doca_devinfo_rep **rep_list = NULL;
    uint32_t nb_reps;
    doca_error_t err;

    err = doca_devinfo_rep_create_list(dev, DOCA_DEVINFO_REP_FILTER_NET, &rep_list, &nb_reps);
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
    struct cache_aligned_data *data
) {
    struct doca_comch_server *server;
    doca_error_t err;

    err = doca_comch_server_create(dev, rep, config->name, &server);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create context: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_ctx *ctx = doca_comch_server_as_ctx(server);
    union doca_data ctx_user_data = { .ptr = data };
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
    doca_comch_server_destroy(server);
failure:
    return NULL;
}
