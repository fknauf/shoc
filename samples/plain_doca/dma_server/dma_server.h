#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_dma.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[%s] " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__)

struct server_config {
    char const *dev_pci;
    char const *dev_rep_pci;
    char const *name;
    uint32_t num_send_tasks;
    uint32_t max_msg_size;
    uint32_t recv_queue_size;
    uint32_t max_buffers;
};

struct cache_aligned_data {
    uint8_t *base_ptr;
    uint32_t block_count;
    uint32_t block_size;
};

struct extents_msg {
    size_t length;
    uint8_t bytes[];
};
struct extents_msg *create_extents_msg(struct cache_aligned_data *data, void const *export_desc, size_t export_desc_len);

struct connection_state {
    struct doca_comch_connection *connection;
    struct doca_mmap *memmap;
};
struct connection_state *create_connection_state(struct doca_comch_connection *connection, struct doca_mmap *memmap);
void destroy_connection_state(struct connection_state *state, _Bool already_disconnected);

void server_state_change_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void connection_callback(struct doca_comch_event_connection_status_changed *event, struct doca_comch_connection *connection, uint8_t change_successful);
void disconnection_callback(struct doca_comch_event_connection_status_changed *event, struct doca_comch_connection *connection, uint8_t change_successful);
void msg_recv_callback(struct doca_comch_event_msg_recv *event, uint8_t *recv_buffer, uint32_t msg_len, struct doca_comch_connection *connection);
void send_task_completed_callback(struct doca_comch_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);
void send_task_error_callback(struct doca_comch_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);

struct doca_pe *open_progress_engine(int epoll_fd);
struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions);
struct doca_buf_inventory *open_buffer_inventory(uint32_t max_buffers);

struct doca_dev *open_device(char const *pci_addr);
struct doca_dev_rep *open_device_representor(struct doca_dev *dev, char const *pci_addr);
struct doca_comch_server *open_server_context(struct doca_pe *engine, struct doca_dev *dev, struct doca_dev_rep *rep, struct server_config *config, struct cache_aligned_data *data);

void serve_dma(struct server_config *server_config, struct cache_aligned_data *data);
