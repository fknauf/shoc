#pragma once

#include "common.hpp"
#include <shoc/buffer.hpp>
#include <shoc/context.hpp>
#include <shoc/coro/value_awaitable.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/unique_handle.hpp>

#include <doca_comch_consumer.h>
#include <doca_pe.h>

#include <boost/container/static_vector.hpp>

#include <memory>

/**
 * Consumer class for the DOCA Comch fast path, see https://docs.nvidia.com/doca/sdk/doca+comch/index.html
 *
 * A consumer on one side is associated with a producer on the other, such that the producer sends buffers
 * that a consumer receives. The consumer should generally already be listening by the time the producer
 * attempts to send because the producer attempts a DMA copy (that's why it's the fast path) for which a
 * suitable data buffer needs to be set up on the consumer side.
 */
namespace shoc::comch {
    class consumer;

    /**
     * Information associated with a completed consumer receive task, i.e. immediate data sent
     * by the producer, the ID of the producer, and error information
     */
    struct consumer_recv_result {
        boost::container::static_vector<std::byte, MAX_IMMEDIATE_DATA_SIZE> immediate;
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
        public context<
            doca_comch_consumer,
            doca_comch_consumer_destroy,
            doca_comch_consumer_as_ctx
        >
    {
    public:
        consumer(
            context_parent *parent,
            doca_comch_connection *connection,
            memory_map &user_mmap,
            std::uint32_t max_tasks
        );

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
    };
}
