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
#include <unordered_map>

namespace shoc {
    /**
     * Configuration parameters of an RDMA offloading context
     */
    struct rdma_config {
        std::uint32_t rdma_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
        std::optional<std::uint32_t> gid_index = std::nullopt;
        std::uint32_t max_tasks = 16;
        std::uint16_t max_num_connections = 1;
        doca_rdma_transport_type transport_type = DOCA_RDMA_TRANSPORT_TYPE_RC;
    };

    class rdma_context;

    /**
     * Address for RDMA CM connections
     */
    class rdma_address {
    public:
        friend class rdma_context;

        rdma_address() = default;

        rdma_address(
            doca_rdma_addr_type addr_type,
            char const *address,
            std::uint16_t port
        );

        [[nodiscard]]
        auto handle() const noexcept {
            return addr_.get();
        }

        [[nodiscard]]
        auto addr_type() const -> doca_rdma_addr_type {
            return params().addr_type;
        }

        [[nodiscard]]
        auto address() const -> char const * {
            return params().address;
        }

        [[nodiscard]]
        auto port() const -> std::uint16_t {
            return params().port;
        }

    private:
        struct params_type {
            doca_rdma_addr_type addr_type;
            char const *address;
            std::uint16_t port;
        };

        [[nodiscard]]
        auto params() const -> params_type;

        unique_handle<doca_rdma_addr, doca_rdma_addr_destroy> addr_;
    };

    enum rdma_cm_role {
        none,
        server,
        client
    };

    /**
     * Encapsulation of an RDMA connection. Obtained from rdma_context. Most tasks
     * are offloaded here.
     */
    class rdma_connection {
    public:
        rdma_connection(rdma_context *parent);

        // for RDMA CM, where the doca_rdma_connection object already exists
        rdma_connection(rdma_context *parent, doca_rdma_connection *cm_conn) noexcept;

        /**
         * Establish connection with remote when connection details are exchanged out
         * of band (without CM)
         * 
         * When connecting without CM, the connection is exported from rdma_context and
         * connection details are exchanged out of band. After that the connection is
         * established using the remote connection details. This function does that.
         * 
         * @param remote_details connection details as exported on the remote side
         */
        auto connect(std::span<std::byte const> remote_details) -> void;
        auto connect(std::string_view remote_details) -> void;

        /**
         * @return exported connection details for out-of-band exchange (when not using CM)
         */
        [[nodiscard]]
        auto details() const noexcept {
            return details_;
        }

        /**
         * IB send verb
         * 
         * @param src buffer to send
         */
        auto send(
            buffer const &src
        ) -> coro::status_awaitable<>;

        /**
         * IB send_imm verb
         * 
         * @param src buffer to send
         * @param immediate_data immediate data to send alongside the buffer
         */
        auto send(
            buffer const &src,
            std::uint32_t immediate_data
        ) -> coro::status_awaitable<>;

        /**
         * IB receive verb. Must be called before the remote sends.
         * 
         * @param dest destination buffer to receive the data
         * @param immediate_data pointer to a buffer for immediate data, or nullptr if none
         */
        auto receive(
            buffer &dest,
            std::uint32_t *immediate_data = nullptr
        ) -> coro::status_awaitable<std::uint32_t>;

        /**
         * IB read verb: read remote memory location to local
         * 
         * @param src remote source buffer
         * @param dest local destination buffer
         */
        auto read(
            buffer const &src,
            buffer &dest
        ) -> coro::status_awaitable<>;

        /**
         * IB write: write to remote memory location
         * 
         * @param src local source buffer
         * @param dest remote destination buffer
         */
        auto write(
            buffer const &src,
            buffer &dest
        ) -> coro::status_awaitable<>;

        /**
         * IB write_imm: write to remote memory location
         * 
         * @param src local source buffer
         * @param dest remote destination buffer
         * @param immediate_data immediate data to send alongside the write
         */
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
        unique_handle<doca_rdma_connection, doca_rdma_connection_disconnect> handle_;
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

        [[nodiscard]]
        auto export_connection() -> rdma_connection;

        [[nodiscard]]
        auto listen(std::uint16_t port) -> coro::value_awaitable<rdma_connection>;

        [[nodiscard]]
        auto connect(rdma_address const &peer) -> coro::value_awaitable<rdma_connection>;

    private:
        static auto connection_request     (doca_rdma_connection *conn,                           doca_data ctx_user_data) noexcept -> void;
        static auto connection_established (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) noexcept -> void;
        static auto connection_failure     (doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) noexcept -> void;
        static auto connection_disconnected(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) noexcept -> void;

        static auto receive_completion_callback(
            doca_rdma_task_receive *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        device dev_;

        auto take_connection_receptable(
            doca_rdma_connection *conn,
            doca_data conn_user_data
        ) -> coro::value_receptable<rdma_connection>*;

        std::unordered_map<std::uint16_t, coro::value_receptable<rdma_connection>*> listeners_;
        rdma_cm_role cm_role_ = rdma_cm_role::none;
    };
}