#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

struct client_state {
    struct doca_comch_client *client;
    struct doca_comch_consumer *consumer;
    struct doca_mmap *memmap;
    void *buffer;
    uint32_t buflen;
};

void consumer_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) user_data;
    (void) ctx;

    printf("consumer state change %d -> %d\n", prev_state, next_state);

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        doca_ctx_stop(ctx);
    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        struct client_state *state = user_data.ptr;
        struct doca_ctx *client_ctx = doca_comch_client_as_ctx(state->client);
        doca_ctx_stop(client_ctx);
    }
}

void client_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) prev_state;
    (void) ctx;

    printf("client state change %d -> %d\n", prev_state, next_state);

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct client_state *state = user_data.ptr;
        struct doca_comch_connection *connection;
        doca_comch_client_get_connection(state->client, &connection);

        struct doca_comch_consumer *consumer;
        doca_comch_consumer_create(connection, state->memmap, &consumer);
        
        struct doca_ctx *consumer_ctx = doca_comch_consumer_as_ctx(consumer);
        doca_ctx_set_state_changed_cb(consumer_ctx, consumer_state_change_callback);
        union doca_data consumer_user_data = { .ptr = state };
        doca_ctx_set_user_data(consumer_ctx, consumer_user_data);
        doca_ctx_start(consumer_ctx);
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

struct doca_dev *open_client_device(char const *pci_addr) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;

    doca_devinfo_create_list(&dev_list, &nb_devs);

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);

        if(is_addr_equal && doca_comch_cap_client_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            doca_dev_open(dev_list[i], &result);
            return result;
        }
    }

    exit(-1);
}

int main(void) {
    struct doca_dev *dev = open_client_device("e1:00.0");

    struct doca_pe *pe;
    doca_pe_create(&pe);

    doca_event_handle_t event_handle;
    doca_pe_get_notification_handle(pe, &event_handle);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in);

    struct doca_comch_client *client;
    doca_comch_client_create(dev, "consumer-start-bug", &client);
    doca_comch_client_set_max_msg_size(client, 4080);
    doca_comch_client_set_recv_queue_size(client, 16);
    doca_comch_client_task_send_set_conf(client, send_task_completed_callback, send_task_completed_callback, 16);

    struct doca_ctx *client_ctx = doca_comch_client_as_ctx(client);
    doca_ctx_set_state_changed_cb(client_ctx, client_state_change_callback);

    struct client_state *state = calloc(1, sizeof(struct client_state));
    state->client = client;
    state->buffer = calloc(1024, 1);
    state->buflen = 1024;
    doca_mmap_create(&state->memmap);
    doca_mmap_set_memrange(state->memmap, state->buffer, state->buflen);
    doca_mmap_set_permissions(state->memmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE);
    doca_mmap_add_dev(state->memmap, dev);
    doca_mmap_start(state->memmap);

    union doca_data client_user_data = { .ptr = state };
    doca_ctx_set_user_data(client_ctx, client_user_data);
    doca_pe_connect_ctx(pe, client_ctx);
    doca_ctx_start(client_ctx);

    for(;;) {
        enum doca_ctx_states client_state;
        doca_ctx_get_state(client_ctx, &client_state);

        if(client_state == DOCA_CTX_STATE_IDLE) {
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

    doca_comch_client_destroy(client);
    doca_mmap_destroy(state->memmap);
    free(state->buffer);
    free(state);
    doca_dev_close(dev);
    doca_pe_destroy(pe);
    close(epoll_fd);
}
