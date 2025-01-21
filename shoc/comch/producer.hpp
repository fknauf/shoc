#pragma once

#include "common.hpp"

#include <shoc/buffer.hpp>
#include <shoc/context.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/unique_handle.hpp>

#include <doca_comch_producer.h>

#include <functional>

namespace shoc::comch {
    /**
     * Producer side of a consumer/producer fast data path
     *
     * Used to send buffers to a consumer on the other side of a connection
     */
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
        auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_comch_producer_as_ctx(handle_.get());
        }

        /**
         * Send a data buffer to a specific consumer
         *
         * @param buf buffer to send
         * @param immediate_data some immediate data to send in addition to the buffer
         * @param consumer_id ID of the consumer that'll receive this buffer
         * @return awaitable to co_await the status of the send operation
         */
        auto send(
            buffer buf,
            std::span<std::uint8_t> immediate_data,
            std::uint32_t consumer_id
        ) -> coro::status_awaitable<>;

    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

    private:
        unique_handle<doca_comch_producer, doca_comch_producer_destroy> handle_;
    };
}
