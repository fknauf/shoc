#pragma once

#include "common.hpp"
//#include "consumer.hpp"
#include "device.hpp"
//#include "producer.hpp"

#include <doca/context.hpp>
#include <doca/coro/value_awaitable.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <queue>

namespace doca::comch {
    struct server_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    class server;
    class server_connection;

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

    class server_connection:
        public context_parent
    {
    public:
        friend class server;
        friend class server_disconnect_awaitable;

        enum class state {
            CONNECTED,
            DISCONNECTING,
            DISCONNECTED
        };

        server_connection(doca_comch_connection *con, server *ctx);
        server_connection(server_connection const &) = delete;
        server_connection(server_connection &&) = delete;

        server_connection &operator=(server_connection const &) = delete;
        server_connection &operator=(server_connection &&) = delete;

        ~server_connection();

        [[nodiscard]]
        auto disconnect() -> server_disconnect_awaitable;

        auto send(std::string_view message) -> status_awaitable;
        auto msg_recv() -> message_awaitable;

        auto signal_stopped_child(context *stopped_child) -> void override;
        auto engine() -> progress_engine* override;

    private:
        auto signal_message(std::string_view msg) -> void;
        auto signal_disconnect() -> void;
        auto disconnect_if_able() -> void;

        doca_comch_connection *handle_ = nullptr;
        server *ctx_ = nullptr;

        std::queue<message> pending_messages_;
        std::queue<message_awaitable::payload_type*> pending_receivers_;

        dependent_contexts<> active_children_;
        state state_ = state::CONNECTED;
        std::coroutine_handle<> coro_disconnect_;
    };

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

    class server:
        public context
    {
    public:
        friend class server_connection;

        server(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            device_representor &rep,
            server_limits const &limits = {}
        );

        ~server();

        [[nodiscard]] auto as_ctx() const -> doca_ctx* override {
            return doca_comch_server_as_ctx(handle_.handle());
        }

        auto handle() const noexcept {
            return handle_.handle();
        }

        auto stop() -> context_state_awaitable override;
        auto accept() -> server_connection_awaitable;

    private:
        auto do_stop_if_able() -> void;
        auto signal_disconnect(doca_comch_connection *con) -> void;

        static auto send_completion_entry(
            doca_comch_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        static auto msg_recv_entry(
            doca_comch_event_msg_recv *event,
            std::uint8_t *recv_buffer,
            std::uint32_t msg_len,
            doca_comch_connection *comch_connection
        ) -> void;

        static auto connection_entry(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        static auto disconnection_entry(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        //static auto new_consumer_entry(
        //    doca_comch_event_consumer *event,
        //    doca_comch_connection *comch_connection,
        //    std::uint32_t remote_consumer_id
        //) -> void;

        //static auto expired_consumer_entry(
        //    doca_comch_event_consumer *event,
        //    doca_comch_connection *comch_connection,
        //    std::uint32_t remote_consumer_id
        //) -> void;

        static auto resolve_server(doca_comch_connection *handle) -> server*;
        static auto resolve_server(doca_comch_server *handle) -> server*;
        static auto resolve_connection(doca_comch_connection *handle) -> server_connection*;

        unique_handle<doca_comch_server> handle_ { doca_comch_server_destroy };

        std::queue<std::shared_ptr<server_connection>> pending_connections_;
        std::queue<server_connection_awaitable::payload_type*> pending_accepters_;

        bool stop_requested_ = false;
        std::unordered_map<doca_comch_connection*, std::shared_ptr<server_connection>> open_connections_;
    };
}
