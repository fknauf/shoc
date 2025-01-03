#include "comch_data_client.h"
#include <doca_log.h>

int main(void) {
    //struct doca_log_backend *sdk_log;
    //
    //doca_log_backend_create_standard();
    //doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    //doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    char const *env_dev = getenv("DOCA_DEV_PCI");

    struct client_config config = {
        .dev_pci_addr = env_dev ? env_dev : "81:00.0",
        .server_name = "vss-data-test",
        .num_send_tasks = 32,
        .max_msg_size = 4080,
        .recv_queue_size = 16
    };

    receive_datastream(&config);
}
