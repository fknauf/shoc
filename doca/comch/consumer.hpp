#pragma once

#include <doca/buffer.hpp>
#include <doca/context.hpp>
#include <doca/coro/value_awaitable.hpp>
#include <doca/memory_map.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch_consumer.h>
#include <doca_pe.h>

#include <span>

namespace doca::comch {
    class consumer;

    struct consumer_recv_result {
        buffer buf;
        std::span<std::uint8_t const> immediate;
        std::uint32_t producer_id = -1;
        std::uint32_t status = DOCA_ERROR_EMPTY;
    };

    using consumer_recv_awaitable = coro::value_awaitable<consumer_recv_result>;

    class consumer:
        public context
    {
    public:
        using payload_type = std::span<char>;

        consumer(
            context_parent *parent,
            doca_comch_connection *connection,
            memory_map &user_mmap,
            std::uint32_t max_tasks
        );

        ~consumer();

        [[nodiscard]]
        auto as_ctx() const -> doca_ctx* override {
            return doca_comch_consumer_as_ctx(handle_.handle());
        }

        auto post_recv(buffer dest) -> consumer_recv_awaitable;

    private:
        static auto post_recv_task_completion_entry(
            doca_comch_consumer_task_post_recv *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_comch_consumer> handle_ { doca_comch_consumer_destroy };
    };
}
