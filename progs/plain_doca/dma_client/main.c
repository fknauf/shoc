#include "dma_client.h"

void receive_dma(struct client_config *config) {
    doca_error_t err;
    struct client_state result_buffer = { .parallelism = config->parallelism };

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
    result_buffer.engine = engine;

    struct doca_dev *client_dev = open_device(config->dev_pci_addr);
    if(client_dev == NULL) {
        LOG_ERROR("could not obtain device");
        goto cleanup_pe;
    }
    result_buffer.device = client_dev;

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
            goto cleanup_storage;
        }

        doca_pe_clear_notification(engine, 0);
        while(doca_pe_progress(engine) > 0) {
            // do nothing; doca_pe_progress calls event handlers
        }
    }

    struct timespec start = result_buffer.start;
    struct timespec end = result_buffer.end;
    double elapsed_us = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
    uint32_t bytes_read = result_buffer.data->block_count * result_buffer.data->block_size;
    double data_rate = bytes_read / elapsed_us * 1e6 / (1 << 30);
    _Bool data_error = false;

    char const *skip_verify = getenv("SKIP_VERIFY");
    if(skip_verify == NULL || strcmp(skip_verify, "1") != 0) {
        for(uint32_t i = 0; i < result_buffer.data->block_count; ++i) {
            uint8_t *base = result_buffer.data->base_ptr + i * result_buffer.data->block_size;

            for(uint32_t k = 0; k < result_buffer.data->block_size; ++k) {
                if(base[k] != (uint8_t) i) {
                    LOG_ERROR("Block %" PRIu32 " has invalid data byte %u", i, base[k]);
                    data_error = true;
                    break;
                }
            }
        }
    }

    printf(
        "{\n"
        "  \"data_error\": %s,\n"
        "  \"data_rate_gibps\": %f,\n"
        "  \"elapsed_us\": %f\n"
        "}\n",
        data_error ? "true" : "false",
        data_rate,
        elapsed_us
    );

cleanup_storage:
    free(result_buffer.data);
    doca_comch_client_destroy(client);
cleanup_dev:
    doca_dev_close(client_dev);
cleanup_pe:
    doca_pe_destroy(engine);
cleanup_epoll:
    close(epoll_fd);
end:
}

int main(int argc, char *argv[]) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    struct client_config config = {
        .dev_pci_addr = "81:00.0",
        .server_name = "dma-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16,
        .parallelism = argc < 2 ? 1 : atoi(argv[1])
    };

    receive_dma(&config);
}
