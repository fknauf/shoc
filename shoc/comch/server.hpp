#pragma once

#include "common.hpp"
#include "consumer.hpp"
#include "producer.hpp"

#include <shoc/common/status.hpp>
#include <shoc/context.hpp>
#include <shoc/coro/value_awaitable.hpp>
#include <shoc/device.hpp>
#include <shoc/unique_handle.hpp>

#include <doca_comch.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <queue>

/**
 * Classes related to doca comch server contexts.
 *
 * A server owns several server_connections, on which messages can be sent and received,
 * remote consumers can be accepted and local consumers and producers can be created.
 *
 * So we are looking at a context hierarchy like this:
 *
 * server
 *   +------------\
 *   |            |
 * connection   connection
 *   +--------\         +---------\
 *   |        |         |         |
 * consumer producer   consumer  consumer
 */

namespace shoc::comch {
    class server;
    class server_connection;

    struct server_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    /**
     * utility class to await the disconnection of a server connection
     */
    class server_disconnect_awaitable {
    public:
        server_disconnect_awaitable(server_connection *con):
            con_ { con }
        {}

        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> handle) const noexcept -> void;
        auto await_resume() const noexcept {}

    private:
        server_connection *con_;
    };

    /**
     * One of potentially many connections to a server. Can be used to send and receive messages
     * to/from the client that connected to us, and also for a consumer/producer high-speed
     * data channel.
     */
    class server_connection:
        public context_parent
    {
    public:
        friend class server;
        friend class server_disconnect_awaitable;

        server_connection(doca_comch_connection *con, server *ctx);

        server_connection(server_connection const &) = delete;
        server_connection(server_connection &&) = delete;

        server_connection &operator=(server_connection const &) = delete;
        server_connection &operator=(server_connection &&) = delete;

        ~server_connection();

        [[nodiscard]]
        auto disconnect() -> server_disconnect_awaitable;

        /**
         * Send message to the connected client. Returns an awaitable with with the result
         * of the operation (success or reason for failure) can be co_awaited.
         *
         * @param msg message base address
         * @param len message length
         * @return awaitable for the send operation status result
         */
        auto send(char const *message_cstr) -> coro::status_awaitable<>;
        auto send(std::span<char const> message) -> coro::status_awaitable<>;
        auto send(std::span<std::uint8_t const> message) -> coro::status_awaitable<>;
        auto send(std::span<std::byte const> message) -> coro::status_awaitable<>;

        /**
         * Receive message from the connected client. Returns an awaitable with which a message
         * can be co_awaited. If the connection is disconnected before a message is received,
         * the co_await will throw a doca_exception with the reason for failure.
         *
         * @return Awaitable for the received message
         */
        auto msg_recv() -> message_awaitable;

        /**
         * Creates a consumer child context to receive data buffers from a producer on the client side.
         *
         * @param user_mmap Memory map that contains the buffers where this consumer receives data
         * @param max_tasks maximum number of concurrent post_recv tasks on this context
         * @return an awaitable for a scoped, started consumer
         */
        auto create_consumer(memory_map &user_mmap, std::uint32_t max_tasks) {
            return active_children_.create_context<consumer>(this, handle_, user_mmap, max_tasks);
        }

        /**
         * Creates a producer child context to send data buffers to a consumer on the client side
         *
         * @param max_tasks maximum number of concurrent post_recv tasks on this context
         * @return an awaitable for a scoped, started producer
         */
        auto create_producer(std::uint32_t max_tasks) {
            return active_children_.create_context<producer>(this, handle_, max_tasks);
        }

        /**
         * Retrieves the id of a remote consumer that can be used with a producer to send data buffers to the
         * accepted remote consumer.
         *
         * @return an awaitable for a remote consumer id
         */
        auto accept_consumer() -> id_awaitable;

        auto signal_stopped_child(context_base *stopped_child) -> void override;

        [[nodiscard]]
        auto engine() -> progress_engine* override;

    private:
        // signals for the server callbacks to pass event data
        auto signal_message(std::string_view msg) -> void;
        auto signal_disconnect() -> void;
        auto signal_new_consumer(std::uint32_t remote_consumer_id) -> void;

        auto disconnect_if_able() -> void;

        doca_comch_connection *handle_ = nullptr;
        server *ctx_ = nullptr;

        accepter_queues<message> message_queues_;
        accepter_queues<std::uint32_t> remote_consumer_queues_;

        dependent_contexts<> active_children_;
        connection_state state_ = connection_state::CONNECTED;
        std::coroutine_handle<> coro_disconnect_;
    };

    /**
     * Scope wrapper for server connections (for automatic disconnection through RAII)
     *
     * Extremely similar to unique_scoped_context
     */
    class scoped_server_connection {
    public:
        scoped_server_connection() = default;
        scoped_server_connection(std::shared_ptr<server_connection> con):
            con_ { con }
        {}

        scoped_server_connection(scoped_server_connection const&) = delete;
        scoped_server_connection(scoped_server_connection &&other):
            con_ { std::exchange(other.con_, nullptr) }
        {}

        scoped_server_connection &operator=(scoped_server_connection const&) = delete;

        scoped_server_connection &operator=(scoped_server_connection &&other) {
            clear();
            con_ = std::exchange(other.con_, nullptr);
            return *this;
        }

        ~scoped_server_connection() {
            clear();
        }

        auto get() const noexcept { return con_.get(); }
        auto operator->() const noexcept { return get(); }
        auto &operator*() const noexcept { return *get(); }

        explicit operator bool() const noexcept { return con_ != nullptr; }

    private:
        auto clear() -> void{
            if(con_ != nullptr) {
                static_cast<void>(con_->disconnect());
                con_ = nullptr;
            }
        }

        std::shared_ptr<server_connection> con_;
    };

    using server_connection_awaitable = coro::value_awaitable<scoped_server_connection>;

    /**
     * Server context. Main function is to accept server connections.
     */
    class server:
        public context<
            doca_comch_server,
            doca_comch_server_destroy,
            doca_comch_server_as_ctx
        >
    {
    public:
        friend class server_connection;

        server(
            context_parent *parent,
            std::string const &server_name,
            device dev,
            device_representor rep,
            server_limits const &limits = {}
        );

        auto stop() -> context_state_awaitable override;

        /**
         * Accept a client connection. Will throw if the server is stopped before a client
         * connected.
         *
         * @return an awaitable on which a server connection can be co_awaited.
         */
        auto accept() -> server_connection_awaitable;

    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

        [[nodiscard]]
        auto preparing_stop() const noexcept -> bool override {
            return stop_requested_ && doca_state() == DOCA_CTX_STATE_RUNNING;
        }

    private:
        auto do_stop_if_able() -> void;
        auto signal_disconnect(doca_comch_connection *con) -> void;

        static auto send_completion_callback(
            doca_comch_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        static auto msg_recv_callback(
            doca_comch_event_msg_recv *event,
            std::uint8_t *recv_buffer,
            std::uint32_t msg_len,
            doca_comch_connection *comch_connection
        ) -> void;

        static auto connection_callback(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        static auto disconnection_callback(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        static auto new_consumer_callback(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        static auto expired_consumer_callback(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        // Methods to retrieve relevant server/connection objects in a callback function from
        // the parameters the DOCA SDK gives us.
        static auto resolve_server(doca_comch_connection *handle) -> server*;
        static auto resolve_server(doca_comch_server *handle) -> server*;
        static auto resolve_connection(doca_comch_connection *handle) -> server_connection*;

        // dev + rep remembered here to extend their lifetime until
        // after the context handle is destroyed

        device dev_;
        device_representor rep_;

        accepter_queues<
            std::shared_ptr<server_connection>,
            scoped_server_connection
        > connection_queues_;

        bool stop_requested_ = false;
        std::unordered_map<doca_comch_connection*, std::shared_ptr<server_connection>> open_connections_;
    };
}
