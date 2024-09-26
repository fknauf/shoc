#pragma once

#include "common.hpp"
//#include "consumer.hpp"
#include "device.hpp"
//#include "producer.hpp"

#include <doca/context.hpp>
#include <doca/coro/receptable.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch.h>

#include <string>
#include <string_view>
#include <unordered_set>
#include <queue>

namespace doca::comch {
    struct server_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    class server;

    class server_connection {
    public:
        friend class server;

        server_connection() = default;
        server_connection(doca_comch_connection *con, server *ctx);
        server_connection(server_connection const &) = delete;
        server_connection(server_connection &&);

        server_connection &operator=(server_connection const &) = delete;
        server_connection &operator=(server_connection &&);

        ~server_connection();

        auto swap(server_connection &other) noexcept -> void;

        [[nodiscard]]
        auto disconnect() -> doca_error_t;

        auto send(std::string_view message) -> status_awaitable;
        auto msg_recv() -> message_awaitable;

        [[nodiscard]]
        static auto resolve(doca_comch_connection *) -> server_connection*;

    private:
        auto update_user_data() noexcept -> void;
        auto signal_message(std::string_view msg) -> void;
        auto signal_disconnect() -> void;

        doca_comch_connection *handle_ = nullptr;
        server *ctx_ = nullptr;

        std::queue<message> pending_messages_;
        std::queue<message_awaitable::payload_type*> pending_receivers_;
    };

    using server_connection_awaitable = coro::receptable_awaiter<server_connection>;

    class server:
        public context
    {
    public:
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

        auto accept() -> server_connection_awaitable;

    private:
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

        static auto resolve(doca_comch_connection *handle) -> server*;
        static auto resolve(doca_comch_server *handle) -> server*;

        unique_handle<doca_comch_server> handle_ { doca_comch_server_destroy };

        std::queue<server_connection> pending_connections_;
        std::queue<server_connection_awaitable::payload_type*> pending_accepters_;
    };
}
