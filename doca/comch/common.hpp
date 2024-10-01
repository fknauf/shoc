#pragma once

#include <doca/coro/value_awaitable.hpp>

#include <cstdint>
#include <string>
#include <optional>
#include <queue>

#include <doca_comch.h>

namespace doca::comch {
    using message = std::string;

    using status_awaitable = coro::value_awaitable<doca_error_t>;
    using message_awaitable = coro::value_awaitable<message>;
    using id_awaitable = coro::value_awaitable<std::uint32_t>;

    enum class connection_state {
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED
    };

    template<typename Payload, typename ScopeWrapper = Payload>
    class accepter_queues {
    public:
        using awaitable = coro::value_awaitable<ScopeWrapper>;

        accepter_queues() = default;
        accepter_queues(accepter_queues const &) = delete;
        accepter_queues(accepter_queues &&) = default;

        accepter_queues &operator=(accepter_queues const &) = delete;
        accepter_queues &operator=(accepter_queues &&) = default;

        auto accept() -> awaitable {
            if(!pending_data_.empty()) {
                auto result = awaitable::from_value(std::move(pending_data_.front()));
                pending_data_.pop();
                return result;
            } else if(disconnected_) {
                return awaitable::from_error(DOCA_ERROR_NOT_CONNECTED);
            } else {
                auto result = awaitable::create_space();
                pending_accepters_.push(result.dest.get());
                return result;
            }
        }

        auto supply(Payload new_payload) -> void {
            if(pending_accepters_.empty()) {
                pending_data_.emplace(std::move(new_payload));
            } else {
                auto accepter = pending_accepters_.front();
                accepter->value = ScopeWrapper { std::move(new_payload) };
                pending_accepters_.pop();
                accepter->resume();
            }
        }

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
        std::queue<coro::receptable<ScopeWrapper>*> pending_accepters_;
        bool disconnected_ = false;
    };
}
