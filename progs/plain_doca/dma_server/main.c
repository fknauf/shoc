#include "dma_server.h"
#include <doca_log.h>
#include <stdlib.h>

struct cache_aligned_data *create_test_data(
    uint32_t block_count,
    uint32_t block_size
) {
    struct cache_aligned_data *result = malloc(sizeof(struct cache_aligned_data) + block_count * block_size + 64);

    if(result == NULL) {
        LOG_ERROR("unable to allocate memory");
        return NULL;
    }

    result->base_ptr = (uint8_t*) (((uintptr_t) &result[1] + 64) / 64 * 64);
    result->block_count = block_count;
    result->block_size = block_size;

    for(uint32_t i = 0; i < block_count; ++i) {
        uint32_t offset = i * block_size;
        memset(result->base_ptr + offset, (uint8_t) i, block_size);
    }

    return result;
}

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    char const *env_dev = getenv("DOCA_DEV_PCI");
    char const *env_rep = getenv("DOCA_DEV_REP_PCI");

    struct server_config config = {
        .dev_pci = env_dev ? env_dev : "03:00.0",
        .dev_rep_pci = env_rep ? env_rep : "81:00.0",
        .name = "dma-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16,
        .max_buffers = 32
    };

    struct cache_aligned_data *data = create_test_data(256, 1 << 20);
    if(data == NULL) {
        LOG_ERROR("could not allocate test data");
        return -1;
    }

    serve_dma(&config, data);
    free(data);
}
