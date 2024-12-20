#pragma once

#include <doca/buffer.hpp>
#include <doca/context.hpp>
#include <doca/coro/value_awaitable.hpp>
#include <doca/memory_map.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch_consumer.h>
#include <doca_pe.h>

#include <vector>

namespace doca::comch {
    class consumer;

    struct consumer_recv_result {
        std::vector<std::byte> immediate;
        std::uint32_t producer_id = -1;
        doca_error_t status = DOCA_ERROR_EMPTY;
    };

    using consumer_recv_awaitable = coro::value_awaitable<consumer_recv_result>;

    /**
     * Consumer side of a producer/consumer fast data path pair.
     *
     * This class is made to receive data buffers from a producer on the other side of a connection.
     */
    class consumer:
        public context
    {
    public:
        consumer(
            context_parent *parent,
            doca_comch_connection *connection,
            memory_map &user_mmap,
            std::uint32_t max_tasks
        );

        [[nodiscard]]
        auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_comch_consumer_as_ctx(handle_.get());
        }

        /**
         * Receive/wait for a data buffer
         *
         * @return awaitable to co_await a buffer
         */
        auto post_recv(buffer &dest) -> consumer_recv_awaitable;

    private:
        static auto post_recv_task_completion_callback(
            doca_comch_consumer_task_post_recv *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_comch_consumer, doca_comch_consumer_destroy> handle_;
    };
}
