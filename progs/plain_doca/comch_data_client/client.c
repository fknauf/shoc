#include "comch_data_client.h"
#include <doca_dev.h>
#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <inttypes.h>

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

void client_msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;

    uint32_t block_count, block_size;

    if(sscanf((char const*) recv_buffer, "%" PRIu32 " %" PRIu32, &block_count, &block_size) != 2) {
        LOG_ERROR("could not parse incoming message %.*s", msg_len, (char const*) recv_buffer);
        return;
    }

    spawn_consumer(connection, block_count, block_size);
}

void receive_datastream(struct client_config *config) {
    doca_error_t err;
    struct client_state result_buffer;

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

    struct doca_dev *client_dev = open_client_device(config->dev_pci_addr);
    if(client_dev == NULL) {
        LOG_ERROR("could not obtain device");
        goto cleanup_pe;
    }

    struct doca_comch_client *client = open_client_context(engine, client_dev, config, &result_buffer);
    if(client == NULL) {
        LOG_ERROR("could not obtain client context");
        goto cleanup_dev;
    }
    
    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        err = doca_ctx_get_state(doca_comch_client_as_ctx(client), &ctx_state);
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

    struct timespec start = result_buffer.start;
    struct timespec end = result_buffer.end;
    double elapsed_us = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
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

struct doca_dev *open_client_device(char const *pci_addr) {
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

        if(is_addr_equal && doca_comch_cap_client_is_supported(dev_list[i]) == DOCA_SUCCESS) {
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

struct doca_comch_client *open_client_context(
    struct doca_pe *engine,
    struct doca_dev *dev,
    struct client_config *config,
    struct client_state *result_buffer
) {
    struct doca_comch_client *client;
    doca_error_t err;

    err = doca_comch_client_create(dev, config->server_name, &client);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not create context: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct doca_ctx *ctx = doca_comch_client_as_ctx(client);
    err = doca_ctx_set_state_changed_cb(ctx, client_state_changed_callback);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set client state-changed callback: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    union doca_data ctx_user_data = { .ptr = result_buffer };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set client user data: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_set_max_msg_size(client, config->max_msg_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set max message size: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_set_recv_queue_size(client, config->recv_queue_size);
    if(err != DOCA_SUCCESS) {
        LOG_ERROR("could not set receiver queue size: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_comch_client_event_msg_recv_register(client, client_msg_recv_callback);
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
