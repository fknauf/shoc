#pragma once

#include <doca/error.hpp>
#include <doca/logger.hpp>

#include <cassert>
#include <coroutine>
#include <memory>
#include <optional>
#include <utility>

namespace doca::coro {
    template<typename T>
    struct receptable {
        std::optional<T> value;
        std::coroutine_handle<> coro_handle;

        auto resume() {
            if(coro_handle) {
                coro_handle.resume();
            }
        }
    };

    template<typename T>
    struct [[nodiscard]] value_awaitable {
        using payload_type = receptable<T>;

        std::unique_ptr<payload_type> dest;

        value_awaitable() = default;

        value_awaitable(std::unique_ptr<payload_type> &&dest):
            dest { std::move(dest) }
        {}

        static auto create_space() {
            return std::make_unique<payload_type>();
        }

        static auto from_value(T &&val) {
            return value_awaitable(std::make_unique<payload_type>(std::move(val), nullptr));
        }

        auto await_ready() const noexcept -> bool {
            logger->trace("{}", __PRETTY_FUNCTION__);
            // no need to wait if the value is already there.
            return dest && dest->value.has_value();
        }

        auto await_suspend(std::coroutine_handle<> handle) {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, handle.address());

            // this awaiter might be moved around a bit, but co_await should only ever be called
            // on the instance with the value.
            if(dest == nullptr) {
                throw doca_exception { DOCA_ERROR_UNEXPECTED };
            }

            if(dest->value.has_value()) {
                // value already known by the time the awaiter waits, no need to suspend.
                return false;
            } else {
                // value not known yet -> remember waiting coroutine and suspend.
                // the callback will set the value and resume that coroutine.
                dest->coro_handle = handle;
                return true;
            }
        }

        auto await_resume() const noexcept {
            assert(dest);
            assert(dest->value.has_value());

            // only ever called once.
            return std::move(*dest->value);
        }
    };
}