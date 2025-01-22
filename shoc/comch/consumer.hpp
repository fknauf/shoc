#pragma once

#include "common.hpp"
#include <shoc/buffer.hpp>
#include <shoc/context.hpp>
#include <shoc/coro/value_awaitable.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/unique_handle.hpp>

#include <doca_comch_consumer.h>
#include <doca_pe.h>

#include <memory>
#include <vector>

namespace shoc::comch {
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

    class remote_consumer_queues {
    public:
        auto accept() {
            return queues_.accept();
        }

        auto supply(std::uint32_t id) {
            auto payload = std::make_shared<remote_consumer>(id);
            index_[id] = payload;
            return queues_.supply(std::move(payload));
        }

        auto expire(std::uint32_t id) -> void {
            auto iter = index_.find(id);

            if(iter != index_.end()) {
                iter->second->expire();
                index_.erase(iter);
            } else {
                logger->warn("trying to expire unknown remote consumer id {}", id);
            }
        }

        auto disconnect() -> void {
            queues_.disconnect();
        }

    private:
        accepter_queues<shared_remote_consumer> queues_;
        std::unordered_map<std::uint32_t, shared_remote_consumer> index_;
    };
}
