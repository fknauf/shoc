#pragma once

#include <doca/common/status.hpp>
#include <doca/coro/value_awaitable.hpp>

#include <cstdint>
#include <string>
#include <optional>
#include <queue>
#include <type_traits>

#include <doca_comch.h>

namespace doca::comch {
    using message = std::string;

    using message_awaitable = coro::value_awaitable<message>;
    using id_awaitable = coro::value_awaitable<std::uint32_t>;

    enum class connection_state {
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED
    };

    /**
     * Accepter queues for messages, connections, and consumers.
     *
     * When there are no waiting coroutines, messages need to be queued. When there are no
     * queued messages, accepters need to be queued. When the context is stopped, waiting
     * consumers need to be fed an error message. This does that.
     */
    template<typename Payload, typename ScopeWrapper = Payload>
    class accepter_queues {
    public:
        using awaitable = coro::value_awaitable<ScopeWrapper>;

        accepter_queues() = default;
        accepter_queues(accepter_queues const &) = delete;
        accepter_queues(accepter_queues &&) = default;

        accepter_queues &operator=(accepter_queues const &) = delete;
        accepter_queues &operator=(accepter_queues &&) = default;

        /**
         * Accept queued thing or queue an accepter. If the queues are disconnected
         * and empty, the returned awaitable will throw doca_exception upon co_await.
         *
         * @return awaitable to wait for whatever's in this queue.
         */
        auto accept() -> awaitable {
            if(!pending_data_.empty()) {
                auto result = awaitable::from_value(std::move(pending_data_.front()));
                pending_data_.pop();
                return result;
            } else if(disconnected_) {
                return awaitable::from_error(DOCA_ERROR_NOT_CONNECTED);
            } else {
                auto result = awaitable::create_space();
                pending_accepters_.push(result.receptable_ptr());
                return result;
            }
        }

        /**
         * Feed a thing to a waiting accepter or queue the thing for future accepters.
         */
        auto supply(Payload new_payload) -> void {
            if(pending_accepters_.empty()) {
                pending_data_.emplace(std::move(new_payload));
            } else {
                auto accepter = pending_accepters_.front();
                accepter->emplace_value(std::move(new_payload));
                pending_accepters_.pop();
                accepter->resume();
            }
        }

        /**
         * Mark the queues as disconnected and feed an error to all waiting accepters.
         */
        auto disconnect() -> void {
            disconnected_ = true;

            while(!pending_accepters_.empty()) {
                auto accepter = pending_accepters_.front();
                accepter->set_error(DOCA_ERROR_NOT_CONNECTED);
                pending_accepters_.pop();
                accepter->resume();
            }
        }

    private:
        std::queue<Payload> pending_data_;
        std::queue<coro::value_receptable<ScopeWrapper>*> pending_accepters_;
        bool disconnected_ = false;
    };
}
