#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "memory_map.hpp"
#include "progress_engine.hpp"
#include "unique_handle.hpp"

#include <doca_comch_producer.h>

#include <functional>

namespace doca {
    class comch_producer;

    struct comch_producer_callbacks {
        using state_changed_callback = std::function<void(
            comch_producer &self,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        )>;
        using send_completion_callback = std::function<void(
            comch_producer &self, 
            doca_comch_producer_task_send *task,
            doca_data task_user_data
        )>;

        state_changed_callback state_changed = {};
        send_completion_callback send_completion = {};
        send_completion_callback send_error = {};
    };

    class comch_producer:
        public context
    {
    public:
        comch_producer(
            context_parent<comch_producer> *parent,
            doca_comch_connection *connection,
            std::uint32_t max_tasks,
            comch_producer_callbacks callbacks = {}
        );

        ~comch_producer();

        [[nodiscard]]
        auto as_ctx() const -> doca_ctx* override {
            return doca_comch_producer_as_ctx(handle_.handle());
        }

        auto stop() -> void override;

        auto send(
            buffer buf,
            std::span<std::uint8_t> immediate_data,
            std::uint32_t consumer_id,
            doca_data task_user_data = { .ptr = nullptr }
        ) -> void;

    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

    private:
        static auto send_completion_entry(
            doca_comch_producer_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;
        
        static auto send_error_entry(
            doca_comch_producer_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_comch_producer> handle_ { doca_comch_producer_destroy };
        context_parent<comch_producer> *parent_ = nullptr;
        comch_producer_callbacks callbacks_;
    };
}
