#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[%s] " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__)

struct client_config {
    char const *dev_pci_addr;
    char const *server_name;
    uint32_t num_send_tasks;
    uint32_t max_msg_size;
    uint32_t recv_queue_size;
};

struct cache_aligned_storage {
    uint32_t block_count;
    uint32_t block_size;
    uint8_t *bytes;
};

struct cache_aligned_storage *create_cache_aligned_storage(uint32_t block_count, uint32_t block_size);
static inline void destroy_cache_aligned_storage(struct cache_aligned_storage *storage) {
    free(storage);
}
static inline uint8_t *cache_aligned_storage_block(struct cache_aligned_storage *storage, uint32_t blocknum) {
    return storage->bytes + blocknum * storage->block_size;
}

struct client_state {
    struct doca_comch_client *client;
    struct doca_dev *device;
    struct doca_pe *engine;
    struct cache_aligned_storage *result;

    struct timespec start;
    struct timespec end;
};

struct consumer_state {
    struct client_state *client_state;

    struct doca_comch_consumer *consumer;
    struct doca_mmap *memory_map;
    struct doca_buf_inventory *buf_inv;

    uint32_t offloaded;
    uint32_t completed;
};

struct consumer_state *create_consumer_state(
    struct client_state *client_state,
    struct cache_aligned_storage *storage
);

void destroy_consumer_state(struct consumer_state *state);

void client_state_changed_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void client_send_callback(struct doca_comch_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);
void client_msg_recv_callback(struct doca_comch_event_msg_recv *event, uint8_t *recv_buffer, uint32_t msg_len, struct doca_comch_connection *connection);

void consumer_state_change_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void consumer_recv_completed_callback(struct doca_comch_consumer_task_post_recv *task, union doca_data task_user_data, union doca_data ctx_user_data);
void consumer_recv_error_callback(struct doca_comch_consumer_task_post_recv *task, union doca_data task_user_data, union doca_data ctx_user_data);

struct doca_pe *open_progress_engine(int epoll_fd);
struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions);
struct doca_buf_inventory *open_buffer_inventory(uint32_t max_buffers);
struct doca_dev *open_client_device(char const *pci_addr);
struct doca_comch_client *open_client_context(struct doca_pe *engine, struct doca_dev *dev, struct client_config *config, struct client_state *result_buffer);
struct doca_comch_consumer *open_consumer(struct doca_comch_connection *connection, struct consumer_state *state);

void spawn_consumer(struct doca_comch_connection *connection, uint32_t block_count, uint32_t block_size);
void receive_datastream(struct client_config *config);
