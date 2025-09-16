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

/**
 * Producer for the DOCA Comch fast path, sends data buffers by DMA to remote consumers.
 */
namespace shoc::comch {
    /**
     * Producer-side representation of a remote consumer, consists of a unique identifier and
     * the information whether the consumer has expired on the remote.
     */
    class remote_consumer {
    public:
        remote_consumer(
            std::uint32_t id
        ):
            id_ { id }
        { }

        remote_consumer(remote_consumer const&) = delete;
        remote_consumer(remote_consumer &&) = delete;
        remote_consumer &operator=(remote_consumer const &) = delete;
        remote_consumer &operator=(remote_consumer &&) = delete;

        [[nodiscard]]
        auto expired() const noexcept {
            return expired_;
        }

        [[nodiscard]]
        auto id() const {
            return id_;
        }

        auto expire() -> void {
            expired_ = true;
        }

    private:
        std::uint32_t id_;
        bool expired_ = false;
    };

    using shared_remote_consumer = std::shared_ptr<remote_consumer>;
    using remote_consumer_awaitable = coro::value_awaitable<shared_remote_consumer>;

    /**
     * Queues to handle remote consumer events.
     *
     * Producer-side events occur when a consumer is created or expires on the remote. These
     * queues queue either fibers that wait to accept a remote consumer or remote consumers
     * waiting for a fiber to accept them.
     *
     * This class keeps track of unexpired consumers to enable their expiry, so that fibers
     * holding the consumer can know it's expired and send operations to expired consumers
     * are not attempted (from the user's perspective they will fail, but we can avoid offloading
     * a task to a remote we already know is dead).
     */
    class remote_consumer_queues {
    public:
        /**
         * Accept or queue to accept a remote consumer
         *
         * @return Awaitable for a remote consumer
         */
        auto accept() {
            return queues_.accept();
        }

        /**
         * Supply a remote consumer to a waiting fiber (or queue it)
         *
         * @param id remote consumer id (part of the event)
         */
        auto supply(std::uint32_t id) {
            auto payload = std::make_shared<remote_consumer>(id);
            index_[id] = payload;
            return queues_.supply(std::move(payload));
        }

        /**
         * Expire an existing remote consumer
         *
         * @param id remote consumer id of the expired remote consumer
         */
        auto expire(std::uint32_t id) -> void {
            auto iter = index_.find(id);

            if(iter != index_.end()) {
                iter->second->expire();
                index_.erase(iter);
            } else {
                logger->warn("trying to expire unknown remote consumer id {}", id);
            }
        }

        /**
         * Disconnect backend queues. This happens when a disconnect is requested on the
         * parent server/connection and it ceases to accept new consumers. Existing consumers
         * are left alone because actual connection/client shutdown can only occur after they
         * stop.
         */
        auto disconnect() -> void {
            queues_.disconnect();
        }

    private:
        accepter_queues<shared_remote_consumer> queues_;
        std::unordered_map<std::uint32_t, shared_remote_consumer> index_;
    };

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
         * Send a data buffer to a specific consumer. Note that this expects a consumer to be
         * listening for a send task on the remote side, so ideally this should be called when
         * we know a consumer is already listening.
         *
         * If it isn't, this offload will behave as in the DOCA comch samples, i.e. spin for a
         * while in hopes that a consumer will show up to accept the data. The exact behavior
         * of this can be configured in the progress engine.
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
