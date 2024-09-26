#pragma once

#include "common.hpp"

#include <doca/buffer.hpp>
#include <doca/context.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch_producer.h>

#include <functional>

namespace doca::comch {
    class producer:
        public context
    {
    public:
        producer(
            context_parent *parent,
            doca_comch_connection *connection,
            std::uint32_t max_tasks
        );

        ~producer();

        [[nodiscard]]
        auto as_ctx() const -> doca_ctx* override {
            return doca_comch_producer_as_ctx(handle_.handle());
        }

        auto send(
            buffer buf,
            std::span<std::uint8_t> immediate_data,
            std::uint32_t consumer_id
        ) -> status_awaitable;

    private:
        static auto send_completion_entry(
            doca_comch_producer_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_comch_producer> handle_ { doca_comch_producer_destroy };
    };
}
