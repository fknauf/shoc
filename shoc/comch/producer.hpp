#pragma once

#include "common.hpp"
#include "consumer.hpp"

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
        public context<
            doca_comch_producer,
            doca_comch_producer_destroy,
            doca_comch_producer_as_ctx
        >
    {
    public:
        producer(
            context_parent *parent,
            doca_comch_connection *connection,
            std::uint32_t max_tasks
        );

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
            shared_remote_consumer const &destination
        ) -> coro::status_awaitable<>;
    };
}
