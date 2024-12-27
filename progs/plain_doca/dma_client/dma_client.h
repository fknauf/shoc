#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_dma.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <sys/epoll.h>

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[%s] " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__)

struct cache_aligned_data {
    uint8_t *base_ptr;
    uint32_t block_count;
    uint32_t block_size;
};

struct cache_aligned_data *create_cache_aligned_buffer(uint32_t block_count, uint32_t block_size);

struct client_config {
    char const *dev_pci_addr;
    char const *server_name;
    uint32_t num_send_tasks;
    uint32_t max_msg_size;
    uint32_t recv_queue_size;
};

struct client_state {
    struct doca_dev *device;
    struct doca_pe *engine;
    struct doca_comch_client *client;
    struct cache_aligned_data *data;
    struct timespec start;
    struct timespec end;
};

struct dma_state {
    struct client_state *client_state;
    struct doca_dma *dma;

    struct doca_mmap *local_mmap;
    struct doca_mmap *remote_mmap;
    struct doca_buf_inventory *buf_inv;

    uint8_t const *remote_base;

    uint32_t offloaded;
    uint32_t completed;
};

struct dma_state *attach_dma_state(
    struct doca_dma *dma,
    struct client_state *client_state,
    uint8_t *recv_buffer,
    uint32_t recv_len
);

void destroy_dma_state(struct dma_state *dma_state);

void client_state_changed_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void client_send_completed_callback(struct doca_comch_task_send *send_task, union doca_data task_user_data, union doca_data ctx_user_data);
void client_send_error_callback(struct doca_comch_task_send *send_task, union doca_data task_user_data, union doca_data ctx_user_data);
void client_msg_recv_callback(struct doca_comch_event_msg_recv *event, uint8_t *recv_buffer, uint32_t msg_len, struct doca_comch_connection *connection);

void dma_state_changed_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void dma_memcpy_completed_callback(struct doca_dma_task_memcpy *memcpy_task, union doca_data task_user_data, union doca_data ctx_user_data);
void dma_memcpy_error_callback(struct doca_dma_task_memcpy *memcpy_task, union doca_data task_user_data, union doca_data ctx_user_data);

struct doca_pe *open_progress_engine(int epoll_fd);
struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions);
struct doca_buf_inventory *open_buffer_inventory(uint32_t max_buffers);
struct doca_dev *open_device(char const *pci_addr);
struct doca_comch_client *open_client_context(struct doca_pe *engine, struct doca_dev *dev, struct client_config *config, struct client_state *state);
struct doca_dma *open_dma_context(struct client_state *state, uint8_t *extents_message, uint32_t extents_msglen);

void send_done_message(struct client_state *client_state);
void receive_dma(struct client_config *config);
