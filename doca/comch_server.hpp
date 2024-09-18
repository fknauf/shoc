#pragma once

#include "comch_consumer.hpp"
#include "comch_device.hpp"
#include "comch_producer.hpp"
#include "context.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_comch.h>

#include <functional>
#include <span>
#include <string>

namespace doca {
    struct comch_server_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    class base_comch_server:
        public context
    {
    public:
        base_comch_server(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            device_representor &rep,
            comch_server_limits const &limits
        );

        ~base_comch_server();

        [[nodiscard]] auto as_ctx() const -> doca_ctx* override {
            return doca_comch_server_as_ctx(handle_.handle());
        }

        auto send_response(doca_comch_connection *con, std::string_view message) -> void;

        auto stop() -> void override;

        template<typename... Args>
        auto create_producer(Args&&... args) {
            return active_children_.create_context<doca::comch_producer>(this, std::forward<Args>(args)...);
        }

        template<typename... Args>
        auto create_consumer(Args&&... args) {
            return active_children_.create_context<doca::comch_consumer>(this, std::forward<Args>(args)...);
        }

    protected:
        virtual auto send_completion(
            [[maybe_unused]] doca_comch_task_send *task,
            [[maybe_unused]] doca_data task_user_data
        ) -> void {
        }
        
        virtual auto send_error(
            [[maybe_unused]] doca_comch_task_send *task,
            [[maybe_unused]] doca_data task_user_data
        ) -> void {
        }
        
        virtual auto msg_recv(
            [[maybe_unused]] std::span<std::uint8_t> recv_buffer,
            [[maybe_unused]] doca_comch_connection *comch_connection
        ) -> void {
        }

        virtual auto server_connection(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint8_t change_successful
        ) -> void {
        }

        virtual auto server_disconnection(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint8_t change_successful
        ) -> void {
        }

        virtual auto new_consumer(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint32_t remote_consumer_id
        ) -> void {
        }

        virtual auto expired_consumer(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint32_t remote_consumer_id
        ) -> void {
        }

        auto signal_stopped_child(context *stopped_child) -> void override;

    private:
        static auto send_completion_entry(
            doca_comch_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        static auto send_error_entry(
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

        static auto server_connection_entry(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        static auto server_disconnection_entry(
            doca_comch_event_connection_status_changed *event,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void;

        static auto state_changed_entry(
            doca_data user_data,
            doca_ctx *ctx,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void;

        static auto new_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        static auto expired_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        unique_handle<doca_comch_server> handle_ { doca_comch_server_destroy };

        auto do_stop_if_able() -> void;

        dependent_contexts<context> active_children_;
        bool stop_requested_ = false;
    };    

    class comch_server;

    struct comch_server_callbacks {
        using state_changed_callback   = std::function<void(
            comch_server &self,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        )>;
        
        using send_completion_callback = std::function<void(
            comch_server &self,
            doca_comch_task_send *task,
            doca_data task_user_data
        )>;
        
        using msg_recv_callback = std::function<void(
            comch_server &self,
            std::span<std::uint8_t> data_range,
            doca_comch_connection *comch_connection
        )>;
        
        using connection_callback = std::function<void(
            comch_server &self,
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        )>;
        
        using consumer_callback = std::function<void(
            comch_server &self,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        )>;

        state_changed_callback state_changed = {};
        msg_recv_callback message_received = {};
        send_completion_callback send_completion = {};
        send_completion_callback send_error = {};
        connection_callback server_connection = {};
        connection_callback server_disconnection = {};
        consumer_callback new_consumer = {};
        consumer_callback expired_consumer = {};
    };

    class comch_server:
        public base_comch_server
    {
    public:
        comch_server(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            device_representor &rep,
            comch_server_callbacks callbacks,
            comch_server_limits const &limits = {}
        );

    protected:
        auto send_completion(
            doca_comch_task_send *task,
            doca_data task_user_data
        ) -> void override;
        
        auto send_error(
            doca_comch_task_send *task,
            doca_data task_user_data
        ) -> void override;
        
        auto msg_recv(
            std::span<std::uint8_t> recv_buffer,
            doca_comch_connection *comch_connection
        ) -> void override;

        auto server_connection(
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void override;

        auto server_disconnection(
            doca_comch_connection *comch_connection,
            std::uint8_t change_successful
        ) -> void override;

        auto new_consumer(
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void override;

        auto expired_consumer(
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void override;

    private:
        comch_server_callbacks callbacks_;
    };
}
