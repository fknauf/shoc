#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "sync_event.hpp"
#include "unique_handle.hpp"

#include <doca_rdma.h>

#include <cstdint>
#include <optional>

namespace doca {
    class rdma_context:
        public context
    {
        rdma_context(context_parent *parent, device &dev, std::uint32_t max_tasks);

        auto receive(buffer &dest, std::uint32_t *immediate_data = nullptr) -> coro::status_awaitable<std::uint32_t>;
        auto send(buffer const &src) -> coro::status_awaitable<>;
        auto send(buffer const &src, std::uint32_t immediate_data) -> coro::status_awaitable<>;

        auto read(buffer const &src, buffer &dest) -> coro::status_awaitable<>;
        auto write(buffer const &src, buffer &dest) -> coro::status_awaitable<>;
        auto write(buffer const &src, buffer &dest, std::uint32_t immediate_data) -> coro::status_awaitable<>;

        auto atomic_cmp_swp(
            buffer dst,
            buffer result,
            std::uint64_t cmp_data,
            std::uint64_t swap_data
        ) -> coro::status_awaitable<>;

        auto atomic_fetch_add(
            buffer dst,
            buffer result,
            std::uint64_t add_data
        ) -> coro::status_awaitable<>;

        auto remote_net_sync_event_get(
            doca_sync_event_remote_net *event,
            buffer dst
        ) -> coro::status_awaitable<>;
        
        auto remote_net_sync_event_notify_set(
            doca_sync_event_remote_net *event,
            buffer src
        ) -> coro::status_awaitable<>;

        auto remote_net_sync_event_notify_add(
            doca_sync_event_remote_net *event,
            buffer result,
            std::uint64_t add_data
        ) -> coro::status_awaitable<>;

    private:
        static auto connection_request     (doca_rdma_connection *conn,                           doca_data ctx_user_data) -> void;
        static auto connection_established (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;
        static auto connection_failure     (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;
        static auto connection_disconnected(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;

        static auto receive_completion_callback(
            doca_rdma_task_receive *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_rdma, doca_rdma_destroy> handle_;
    };
}