#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_pe.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
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

void send_task_completed_callback(
    struct doca_comch_task_send *task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    doca_task_free(doca_comch_task_send_as_task(task));
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

    doca_devinfo_create_list(&dev_list, &nb_devs);

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);

        if(is_addr_equal && doca_comch_cap_server_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            doca_dev_open(dev_list[i], &result);
            return result;
        }
    }

    exit(-1);
}

struct doca_dev_rep *open_server_device_representor(struct doca_dev *server_dev, char const *pci_addr) {
    struct doca_dev_rep *result = NULL;
    struct doca_devinfo_rep **rep_list = NULL;
    uint32_t nb_reps;

    doca_devinfo_rep_create_list(server_dev, DOCA_DEVINFO_REP_FILTER_NET, &rep_list, &nb_reps);

    for(uint32_t i = 0; i < nb_reps; ++i) {
        uint8_t is_addr_equal;

        doca_devinfo_rep_is_equal_pci_addr(rep_list[i], pci_addr, &is_addr_equal);

        if(is_addr_equal) {
            doca_dev_rep_open(rep_list[i], &result);
            return result;
        }
    }

    exit(-2);
}

int main(void) {
    struct doca_dev *dev = open_server_device("03:00.0");
    struct doca_dev_rep *rep = open_server_device_representor(dev, "e1:00.0");

    struct doca_pe *pe;
    doca_pe_create(&pe);

    doca_event_handle_t event_handle;
    doca_pe_get_notification_handle(pe, &event_handle);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in);

    struct doca_comch_server *server;
    doca_comch_server_create(dev, rep, "consumer-start-bug", &server);
    doca_comch_server_set_max_msg_size(server, 4080);
    doca_comch_server_set_recv_queue_size(server, 16);
    doca_comch_server_task_send_set_conf(server, send_task_completed_callback, send_task_completed_callback, 16);
    doca_comch_server_event_consumer_register(server, new_consumer_callback, expired_consumer_callback);

    struct doca_ctx *server_ctx = doca_comch_server_as_ctx(server);
    doca_ctx_set_state_changed_cb(server_ctx, server_state_change_callback);
    doca_pe_connect_ctx(pe, server_ctx);
    doca_ctx_start(server_ctx);

    for(;;) {
        enum doca_ctx_states server_state;
        doca_ctx_get_state(server_ctx, &server_state);

        if(server_state == DOCA_CTX_STATE_IDLE) {
            break;
        }

        doca_pe_request_notification(pe);
        struct epoll_event ep_event = { 0, { 0 } };
        epoll_wait(epoll_fd, &ep_event, 1, -1);
        doca_pe_clear_notification(pe, 0);
        while(doca_pe_progress(pe) > 0) {
            // do nothing
        }
    }

    doca_comch_server_destroy(server);
    doca_dev_rep_close(rep);
    doca_dev_close(dev);
    doca_pe_destroy(pe);
    close(epoll_fd);
}
