#pragma once

#include "comch_consumer.hpp"
#include "comch_device.hpp"
#include "context.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_comch.h>

#include <functional>
#include <span>
#include <string>

namespace doca {
    struct comch_client_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    class base_comch_client:
        public context
    {
    public:
        base_comch_client(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            comch_client_limits const &limits
        );

        ~base_comch_client();

        [[nodiscard]] auto as_ctx() const -> doca_ctx* override;

        auto submit_message(
            std::string message,
            doca_data task_user_data = { .ptr = nullptr }
        ) -> void;

        [[nodiscard]] auto get_connection() const {
            doca_comch_connection *con;
            enforce_success(doca_comch_client_get_connection(handle_.handle(), &con));
            return con;
        }

        [[nodiscard]] auto handle() const { return handle_.handle(); }

        auto stop() -> void override;
        auto signal_stopped_child(context *stopped_child) -> void override;

        auto create_consumer(
            memory_map &mmap,
            std::uint32_t max_tasks,
            comch_consumer_callbacks callbacks
        ) -> comch_consumer*;

    protected:
        auto do_stop_if_able() -> void;

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

        virtual auto new_consumer(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint32_t id
        ) -> void {
        }

        virtual auto expired_consumer(
            [[maybe_unused]] doca_comch_connection *comch_connection,
            [[maybe_unused]] std::uint32_t id
        ) -> void {
        }

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

        static auto new_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t id
        ) -> void;

        static auto expired_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t id
        ) -> void;

        unique_handle<doca_comch_client> handle_ { doca_comch_client_destroy };
        dependent_contexts<context> active_children_;
        bool stop_requested_ = false;
    };

    class comch_client;

    struct comch_client_callbacks {
        using state_changed_callback = std::function<void(
            comch_client &self, 
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        )>;

        using send_completion_callback = std::function<void(
            comch_client &self, 
            doca_comch_task_send *task,
            doca_data task_user_data
        )>;

        using msg_recv_callback = std::function<void(
            comch_client &self, 
            std::span<std::uint8_t> data_range,
            doca_comch_connection *comch_connection
        )>;

        state_changed_callback state_changed = {};
        msg_recv_callback message_received = {};
        send_completion_callback send_completion = {};
        send_completion_callback send_error = {};
    };

    class comch_client:
        public base_comch_client
    {
    public:
        comch_client(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            comch_client_callbacks callbacks = {},
            comch_client_limits const &limits = {}
        );

    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

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

    private:
        comch_client_callbacks callbacks_;
    };
}
