#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <sys/epoll.h>

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[%s] " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__)

struct server_config {
    char const *server_name;
    uint32_t num_send_tasks;
    uint32_t max_msg_size;
    uint32_t recv_queue_size;
    uint32_t max_buffers;
};

struct data_descriptor {
    void *base_ptr;
    uint32_t block_count;
    uint32_t block_size;
};

struct server_state {
    struct doca_comch_server *server;
    struct doca_pe *engine;
    struct data_descriptor data;
    struct doca_mmap *memory_map;
    struct doca_buf_inventory *buf_inv;
};

struct server_state *create_server_state(
    struct doca_comch_server *server,
    struct doca_pe *engine,
    struct doca_dev *dev,
    struct data_descriptor *data,
    uint32_t max_buffers
);
void destroy_server_state(struct server_state *state);

struct connection_state {
    struct server_state *server_state;

    struct doca_comch_connection *connection;
    struct doca_comch_producer *producer;

    uint32_t remote_consumer_id;
    uint32_t offloaded;
    uint32_t completed;

    struct timespec start;
    struct timespec end;
};

struct connection_state *create_connection_state(
    struct doca_comch_server *server,
    struct doca_comch_connection *connection,
    struct doca_pe *engine
);
void destroy_connection_state(struct connection_state *state);

void producer_state_change_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void producer_send_completed_callback(struct doca_comch_producer_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);
void producer_send_error_callback(struct doca_comch_producer_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);

doca_error_t send_data_extents(struct doca_comch_server *server, struct doca_comch_connection *connection, uint32_t block_count, uint32_t block_size);
doca_error_t send_next_data_buffer(struct connection_state *conn_state);

void server_state_change_callback(union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state, enum doca_ctx_states next_state);
void connection_callback(struct doca_comch_event_connection_status_changed *event, struct doca_comch_connection *connection, uint8_t change_successful);
void disconnection_callback(struct doca_comch_event_connection_status_changed *event, struct doca_comch_connection *connection, uint8_t change_successful);
void new_consumer_callback(struct doca_comch_event_consumer *event, struct doca_comch_connection *connection, uint32_t id);
void expired_consumer_callback(struct doca_comch_event_consumer *event, struct doca_comch_connection *connection, uint32_t id);
void msg_recv_callback(struct doca_comch_event_msg_recv *event, uint8_t *recv_buffer, uint32_t msg_len, struct doca_comch_connection *connection);
void send_task_completed_callback(struct doca_comch_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);
void send_task_error_callback(struct doca_comch_task_send *task, union doca_data task_user_data, union doca_data ctx_user_data);

struct doca_pe *open_progress_engine(int epoll_fd);
struct doca_mmap *open_memory_map(uint8_t *base, size_t size, struct doca_dev *dev, uint32_t permissions);
struct doca_buf_inventory *open_buffer_inventory(uint32_t max_buffers);
struct doca_dev *open_server_device(char const *pci_addr);
struct doca_dev_rep *open_server_device_representor(struct doca_dev *server_dev, char const *pci_addr);
struct doca_comch_server *open_server_context(struct doca_pe *engine, struct doca_dev *dev, struct doca_dev_rep *rep, struct server_config *config, struct data_descriptor *data);
struct doca_comch_producer *open_producer(struct doca_comch_connection *connection, struct connection_state *conn_state, struct doca_pe *engine, uint32_t max_send_tasks);

void serve_datastream(char const *dev_pci, char const *rep_pci, struct server_config *config, struct data_descriptor *data);
