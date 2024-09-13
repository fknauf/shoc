#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "memory_map.hpp"
#include "unique_handle.hpp"

#include <doca_comch_consumer.h>
#include <doca_pe.h>

#include <functional>

namespace doca {
    class comch_consumer;

    class comch_consumer_task_post_recv {
    public:
        comch_consumer_task_post_recv(
            doca_comch_consumer_task_post_recv *handle,
            doca_data task_user_data
        ):
            handle_ { handle },
            buf_ { doca_comch_consumer_task_post_recv_get_buf(handle) },
            user_data_ { task_user_data }
        { }

        ~comch_consumer_task_post_recv() {
            doca_task_free(doca_comch_consumer_task_post_recv_as_task(handle_));
        }

        auto buf() const {
            return buf_;
        }

        auto immediate_data() const -> std::span<std::uint8_t> {
            return {
                doca_comch_consumer_task_post_recv_get_imm_data(handle_),
                doca_comch_consumer_task_post_recv_get_imm_data_len(handle_)
            };
        }

        auto producer_id() const {
            return doca_comch_consumer_task_post_recv_get_producer_id(handle_);
        }

        auto user_data() const {
            return user_data_;
        }

    private:
        doca_comch_consumer_task_post_recv *handle_;
        buffer buf_;
        doca_data user_data_;
    };

    struct comch_consumer_callbacks {
        using post_recv_completion_callback = std::function<void (
            comch_consumer &consumer,
            comch_consumer_task_post_recv &task
        )>;

        using state_changed_callback = std::function<void (
            comch_consumer &consumer,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        )>;

        post_recv_completion_callback post_recv_completion = {};
        post_recv_completion_callback post_recv_error = {};
        state_changed_callback state_changed = {};
    };

    class comch_consumer:
        public context
    {
    public:
        using payload_type = std::span<char>;

        comch_consumer(
            context_parent<comch_consumer> *parent,
            doca_comch_connection *connection,
            memory_map &user_mmap,
            std::uint32_t max_tasks,
            comch_consumer_callbacks callbacks
        );

        ~comch_consumer();

        auto stop() -> void override;

        [[nodiscard]]
        auto as_ctx() const -> doca_ctx* override {
            return doca_comch_consumer_as_ctx(handle_.handle());
        }

        auto post_recv(buffer dest, doca_data task_user_data = { .ptr = nullptr }) -> void;
    
    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

    private:
        static auto post_recv_task_completion_entry(
            doca_comch_consumer_task_post_recv *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        static auto post_recv_task_error_entry(
            doca_comch_consumer_task_post_recv *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_comch_consumer> handle_ { doca_comch_consumer_destroy };
        context_parent<comch_consumer> *parent_ = nullptr;
        comch_consumer_callbacks callbacks_;
    };
}
