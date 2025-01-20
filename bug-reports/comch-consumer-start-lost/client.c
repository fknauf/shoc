#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#define ASSERT_SUCCESS(expr) do { \
    doca_error_t err = (expr); \
    if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) { \
        printf("Error in %s, line %d: %s\n", __func__, __LINE__, doca_error_get_name(err)); \
        fflush(stdout); \
        exit(-3); \
    } \
} while(0)

struct client_state {
    struct doca_comch_client *client;
    struct doca_comch_consumer *consumer;
    struct doca_pe *engine;
    struct doca_mmap *memmap;
    struct doca_buf_inventory *bufinv;
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

    struct client_state *state = user_data.ptr;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        ASSERT_SUCCESS(doca_ctx_stop(ctx));
    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        ASSERT_SUCCESS(doca_comch_consumer_destroy(state->consumer));
        state->consumer = NULL;

        // this fails too even though the consumer is already gone, but it's not the main problem.
        struct doca_ctx *client_ctx = doca_comch_client_as_ctx(state->client);
        ASSERT_SUCCESS(doca_ctx_stop(client_ctx));
    }
}

void consumer_recv_callback(
    struct doca_comch_consumer_task_post_recv *recv_task,
    union doca_data task_user_data,
    union doca_data ctx_user_data
) {
    (void) task_user_data;
    (void) ctx_user_data;

    struct doca_buf *buf = doca_comch_consumer_task_post_recv_get_buf(recv_task);
    struct doca_task *task = doca_comch_consumer_task_post_recv_as_task(recv_task);
    doca_error_t status = doca_task_get_status(task);

    printf("post_recv task finished with status %d\n", status);

    doca_task_free(task);
    doca_buf_dec_refcount(buf, NULL);
}

void client_state_change_callback(
    union doca_data user_data,
    struct doca_ctx *ctx,
    enum doca_ctx_states prev_state,
    enum doca_ctx_states next_state
) {
    (void) ctx;

    printf("client state change %d -> %d\n", prev_state, next_state);

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct client_state *state = user_data.ptr;
        struct doca_comch_connection *connection;
        ASSERT_SUCCESS(doca_comch_client_get_connection(state->client, &connection));

        struct doca_comch_task_send *send_task;
        ASSERT_SUCCESS(doca_comch_client_task_send_alloc_init(state->client, connection, "world", 5, &send_task));
        ASSERT_SUCCESS(doca_task_submit(doca_comch_task_send_as_task(send_task)));

        ASSERT_SUCCESS(doca_comch_consumer_create(connection, state->memmap, &state->consumer));
        ASSERT_SUCCESS(doca_comch_consumer_task_post_recv_set_conf(state->consumer, consumer_recv_callback, consumer_recv_callback, 16));

        struct doca_ctx *consumer_ctx = doca_comch_consumer_as_ctx(state->consumer);
        ASSERT_SUCCESS(doca_ctx_set_state_changed_cb(consumer_ctx, consumer_state_change_callback));
        union doca_data consumer_user_data = { .ptr = state };
        ASSERT_SUCCESS(doca_ctx_set_user_data(consumer_ctx, consumer_user_data));
        ASSERT_SUCCESS(doca_pe_connect_ctx(state->engine, consumer_ctx));

        ASSERT_SUCCESS(doca_ctx_start(consumer_ctx));
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

struct doca_dev *open_client_device(char const *pci_addr) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;

    ASSERT_SUCCESS(doca_devinfo_create_list(&dev_list, &nb_devs));

    for(uint32_t i = 0; i < nb_devs; ++i) {
        uint8_t is_addr_equal = 0;

        ASSERT_SUCCESS(doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal));

        if(is_addr_equal && doca_comch_cap_client_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            ASSERT_SUCCESS(doca_dev_open(dev_list[i], &result));
            return result;
        }
    }

    exit(-1);
}

int main(void) {
    struct doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stdout, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    char const *dev_pci = getenv("DOCA_DEV");

    struct doca_dev *dev = open_client_device(dev_pci ? dev_pci : "e1:00.0");

    struct doca_pe *pe;
    ASSERT_SUCCESS(doca_pe_create(&pe));

    doca_event_handle_t event_handle;
    ASSERT_SUCCESS(doca_pe_get_notification_handle(pe, &event_handle));

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in);

    struct doca_comch_client *client;
    ASSERT_SUCCESS(doca_comch_client_create(dev, "consumer-start-bug", &client));
    ASSERT_SUCCESS(doca_comch_client_set_max_msg_size(client, 4080));
    ASSERT_SUCCESS(doca_comch_client_set_recv_queue_size(client, 16));
    ASSERT_SUCCESS(doca_comch_client_task_send_set_conf(client, send_task_completed_callback, send_task_completed_callback, 16));
    ASSERT_SUCCESS(doca_comch_client_event_msg_recv_register(client, msg_recv_callback));

    struct doca_ctx *client_ctx = doca_comch_client_as_ctx(client);
    ASSERT_SUCCESS(doca_ctx_set_state_changed_cb(client_ctx, client_state_change_callback));

    struct client_state *state = calloc(1, sizeof(struct client_state));
    state->client = client;
    state->engine = pe;
    state->buflen = 1 << 20;
    state->buffer = calloc(state->buflen, 1);
    ASSERT_SUCCESS(doca_mmap_create(&state->memmap));
    ASSERT_SUCCESS(doca_mmap_set_memrange(state->memmap, state->buffer, state->buflen));
    ASSERT_SUCCESS(doca_mmap_set_permissions(state->memmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    ASSERT_SUCCESS(doca_mmap_add_dev(state->memmap, dev));
    ASSERT_SUCCESS(doca_mmap_start(state->memmap));
    ASSERT_SUCCESS(doca_buf_inventory_create(16, &state->bufinv));

    union doca_data client_user_data = { .ptr = state };
    ASSERT_SUCCESS(doca_ctx_set_user_data(client_ctx, client_user_data));
    ASSERT_SUCCESS(doca_pe_connect_ctx(pe, client_ctx));
    ASSERT_SUCCESS(doca_ctx_start(client_ctx));

    for(;;) {
        // comment out this line to make the sample work.
        // When notifications are requested, the consumer start event is lost on BF-3 (but not BF-2)
        ASSERT_SUCCESS(doca_pe_request_notification(pe));
        struct epoll_event ep_event = { 0, { 0 } };
        epoll_wait(epoll_fd, &ep_event, 1, 100);
        ASSERT_SUCCESS(doca_pe_clear_notification(pe, 0));

        while(doca_pe_progress(pe) > 0) {
            // do nothing
        }

        enum doca_ctx_states client_state;
        ASSERT_SUCCESS(doca_ctx_get_state(client_ctx, &client_state));

        if(client_state == DOCA_CTX_STATE_IDLE) {
            break;
        }
    }

    doca_comch_client_destroy(client);
    doca_buf_inventory_destroy(state->bufinv);
    doca_mmap_destroy(state->memmap);
    free(state->buffer);
    free(state);
    doca_dev_close(dev);
    doca_pe_destroy(pe);
    close(epoll_fd);
}
