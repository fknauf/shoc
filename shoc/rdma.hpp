#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "sync_event.hpp"
#include "unique_handle.hpp"

#include <doca_rdma.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>

namespace shoc {
    struct rdma_config {
        std::uint32_t rdma_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
        std::optional<std::uint32_t> gid_index = std::nullopt;
        std::uint32_t max_tasks = 16;
    };

    class rdma_context;

    class rdma_connection {
    public:
        rdma_connection(rdma_context *parent);

        auto connect(std::span<std::byte const> remote_details) -> void;
        auto connect(std::string_view remote_details) -> void;

        [[nodiscard]]
        auto details() const noexcept {
            return details_;
        }

        auto send(
            buffer const &src
        ) -> coro::status_awaitable<>;

        auto send(
            buffer const &src,
            std::uint32_t immediate_data
        ) -> coro::status_awaitable<>;

        auto read(
            buffer const &src,
            buffer &dest
        ) -> coro::status_awaitable<>;

        auto write(
            buffer const &src,
            buffer &dest
        ) -> coro::status_awaitable<>;

        auto write(
            buffer const &src,
            buffer &dest,
            std::uint32_t immediate_data
        ) -> coro::status_awaitable<>;

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
            sync_event_remote_net const &event,
            buffer dst
        ) -> coro::status_awaitable<>;
        
        auto remote_net_sync_event_notify_set(
            sync_event_remote_net const &event,
            buffer src
        ) -> coro::status_awaitable<>;

        auto remote_net_sync_event_notify_add(
            sync_event_remote_net const &event,
            buffer result,
            std::uint64_t add_data
        ) -> coro::status_awaitable<>;

    private:
        rdma_context *parent_ = nullptr;
        std::span<std::byte const> details_;
        doca_rdma_connection *handle_ = nullptr;
    };

    class rdma_context:
        public context<
            doca_rdma,
            doca_rdma_destroy,
            doca_rdma_as_ctx
        >
    {
    public:
        rdma_context(
            context_parent *parent,
            device dev,
            rdma_config config = {}
        );

        auto receive(
            buffer &dest,
            std::uint32_t *immediate_data = nullptr
        ) -> coro::status_awaitable<std::uint32_t>;

        [[nodiscard]]
        auto export_connection() -> rdma_connection;

    private:
        //static auto connection_request     (doca_rdma_connection *conn,                           doca_data ctx_user_data) -> void;
        //static auto connection_established (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;
        //static auto connection_failure     (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;
        //static auto connection_disconnected(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void;

        static auto receive_completion_callback(
            doca_rdma_task_receive *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        device dev_;

        bool connected_ = false;
        coro::status_awaitable<>::payload_type *accept_receptable_ = nullptr;
    };
}