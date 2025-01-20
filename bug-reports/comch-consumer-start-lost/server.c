#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_pe.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#define ASSERT_SUCCESS(expr) do { \
    doca_error_t err = (expr); \
    if(err != DOCA_SUCCESS) { \
        printf("Error in %s, line %d: %s\n", __func__, __LINE__, doca_error_get_name(err)); \
        fflush(stdout); \
        exit(-3); \
    } \
} while(0)

void server_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) user_data;
    (void) ctx;
    (void) prev_state;

    printf("server state change %d -> %d\n", prev_state, next_state);

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        puts("accepting connections");
    }
}

void connected_callback(
    struct doca_comch_event_connection_status_changed *event,
    struct doca_comch_connection *connection,
    uint8_t change_successful
) {
    (void) event;

    printf("new client connected: %p, %" PRIu8 "\n", connection, change_successful);

    if(change_successful) {
        struct doca_comch_server *server = doca_comch_server_get_server_ctx(connection);
        struct doca_comch_task_send *task;

        ASSERT_SUCCESS(doca_comch_server_task_send_alloc_init(server, connection, "hello", 5, &task));
        ASSERT_SUCCESS(doca_task_submit(doca_comch_task_send_as_task(task)));
    }
}

void disconnected_callback(
    struct doca_comch_event_connection_status_changed *event,
    struct doca_comch_connection *connection,
    uint8_t change_successful
) {
    (void) event;

    printf("client disconnected: %p, %" PRIu8 "\n", connection, change_successful);
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

void msg_recv_callback(
    struct doca_comch_event_msg_recv *event,
    uint8_t *recv_buffer,
    uint32_t msg_len,
    struct doca_comch_connection *connection
) {
    (void) event;
    (void) connection;

    printf("received message: ");
    fwrite(recv_buffer, 1, msg_len, stdout);
    puts("");
}

void new_consumer_callback(
    struct doca_comch_event_consumer *event,
    struct doca_comch_connection *connection,
    uint32_t id
) {
    (void) event;
    (void) connection;

    printf("new consumer %" PRIu32 "\n", id);
}

void expired_consumer_callback(
    struct doca_comch_event_consumer *event,
    struct doca_comch_connection *connection,
    uint32_t id
) {
    (void) event;
    (void) connection;

    printf("expired consumer %" PRIu32 "\n", id);
}

struct doca_dev *open_server_device(char const *pci_addr) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;

    ASSERT_SUCCESS(doca_devinfo_create_list(&dev_list, &nb_devs));

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        ASSERT_SUCCESS(doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal));

        if(is_addr_equal && doca_comch_cap_server_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            ASSERT_SUCCESS(doca_dev_open(dev_list[i], &result));
            return result;
        }
    }

    exit(-1);
}

struct doca_dev_rep *open_server_device_representor(struct doca_dev *server_dev, char const *pci_addr) {
    struct doca_dev_rep *result = NULL;
    struct doca_devinfo_rep **rep_list = NULL;
    uint32_t nb_reps;

    ASSERT_SUCCESS(doca_devinfo_rep_create_list(server_dev, DOCA_DEVINFO_REP_FILTER_NET, &rep_list, &nb_reps));

    for(uint32_t i = 0; i < nb_reps; ++i) {
        uint8_t is_addr_equal;

        ASSERT_SUCCESS(doca_devinfo_rep_is_equal_pci_addr(rep_list[i], pci_addr, &is_addr_equal));

        if(is_addr_equal) {
            ASSERT_SUCCESS(doca_dev_rep_open(rep_list[i], &result));
            return result;
        }
    }

    exit(-2);
}

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stdout, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    char const *dev_pci = getenv("DOCA_DEV");
    char const *rep_pci = getenv("DOCA_REP");

    struct doca_dev *dev = open_server_device(dev_pci ? dev_pci : "03:00.0");
    struct doca_dev_rep *rep = open_server_device_representor(dev, rep_pci ? rep_pci: "e1:00.0");

    struct doca_pe *pe;
    ASSERT_SUCCESS(doca_pe_create(&pe));

    doca_event_handle_t event_handle;
    ASSERT_SUCCESS(doca_pe_get_notification_handle(pe, &event_handle));

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in);

    struct doca_comch_server *server;
    ASSERT_SUCCESS(doca_comch_server_create(dev, rep, "consumer-start-bug", &server));
    ASSERT_SUCCESS(doca_comch_server_set_max_msg_size(server, 4080));
    ASSERT_SUCCESS(doca_comch_server_set_recv_queue_size(server, 16));
    ASSERT_SUCCESS(doca_comch_server_task_send_set_conf(server, send_task_completed_callback, send_task_completed_callback, 16));
    ASSERT_SUCCESS(doca_comch_server_event_msg_recv_register(server, msg_recv_callback));
    ASSERT_SUCCESS(doca_comch_server_event_connection_status_changed_register(server, connected_callback, disconnected_callback));
    ASSERT_SUCCESS(doca_comch_server_event_consumer_register(server, new_consumer_callback, expired_consumer_callback));

    struct doca_ctx *server_ctx = doca_comch_server_as_ctx(server);
    ASSERT_SUCCESS(doca_ctx_set_state_changed_cb(server_ctx, server_state_change_callback));
    ASSERT_SUCCESS(doca_pe_connect_ctx(pe, server_ctx));
    ASSERT_SUCCESS(doca_ctx_start(server_ctx));

    for(;;) {
        ASSERT_SUCCESS(doca_pe_request_notification(pe));
        struct epoll_event ep_event = { 0, { 0 } };
        epoll_wait(epoll_fd, &ep_event, 1, -1);
        ASSERT_SUCCESS(doca_pe_clear_notification(pe, 0));
        while(doca_pe_progress(pe) > 0) {
            // do nothing
        }

        enum doca_ctx_states server_state;
        ASSERT_SUCCESS(doca_ctx_get_state(server_ctx, &server_state));

        if(server_state == DOCA_CTX_STATE_IDLE) {
            break;
        }
    }

    doca_comch_server_destroy(server);
    doca_dev_rep_close(rep);
    doca_dev_close(dev);
    doca_pe_destroy(pe);
    close(epoll_fd);
}
