#include "../env.h"

#include "comch_data_server.h"
#include <doca_log.h>
#include <stdlib.h>

struct data_descriptor *prepare_data(uint32_t block_count, uint32_t block_size) {
    struct data_descriptor *desc = malloc(
        sizeof(struct data_descriptor)
        + block_count * block_size
        + 128);

    if(desc == NULL) {
        return NULL;
    }

    // align to 64 bytes
    uintptr_t base_address = ((uintptr_t) desc + sizeof *desc + 64) / 64 * 64;
    desc->base_ptr = (void*) base_address;
    desc->block_count = block_count;
    desc->block_size = block_size;

    // init with patterns
    uint8_t *block_base = desc->base_ptr;
    for(uint32_t i = 0; i < block_count; ++i) {
        memset(block_base, (int) i, block_size);
        block_base += block_size;
    }

    return desc;
}

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    struct server_config config = {
        .dev_pci = env_device_pci_address(),
        .rep_pci = env_device_representor_pci_address(),
        .server_name = "shoc-data-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16,
        .max_buffers = 32
    };

    struct data_descriptor *data = prepare_data(256, 1048576);

    if(data != NULL) {
        serve_datastream(&config, data);
        free(data);
    }
}
