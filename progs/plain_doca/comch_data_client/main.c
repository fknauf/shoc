#include "comch_data_client.h"
#include <doca_log.h>
#include <stdlib.h>

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    struct client_config config = {
        .dev_pci_addr = "81:00.0",
        .server_name = "vss-data-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16
    };

    receive_datastream(&config);
}
