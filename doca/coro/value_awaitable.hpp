#pragma once

#include "overload.hpp"

#include <doca/error.hpp>
#include <doca/logger.hpp>

#include <cassert>
#include <coroutine>
#include <memory>
#include <utility>
#include <variant>

namespace doca::coro {
    template<typename T>
    struct receptable {
        std::variant<std::monostate, std::exception_ptr, T> value;
        std::coroutine_handle<> coro_handle;

        template<typename... Args>
        auto emplace_value(Args &&...args) {
            value.template emplace<T>(std::forward<Args>(args)...);
        }

        auto set_exception(std::exception_ptr ex) {
            value = ex;
        }

        auto set_error(doca_error_t err) {
            value = std::make_exception_ptr(doca_exception { err });
        }

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
            return value_awaitable { std::make_unique<payload_type>() } ;
        }

        static auto from_value(T &&val) {
            return value_awaitable(std::make_unique<payload_type>(std::move(val), nullptr));
        }

        static auto from_exception(std::exception_ptr ex) {
            return value_awaitable(std::make_unique<payload_type>(ex, nullptr));
        }

        static auto from_error(doca_error_t err) {
            return from_exception(std::make_exception_ptr(doca_exception { err }));
        }

        auto await_ready() const noexcept -> bool {
            logger->trace("{}", __PRETTY_FUNCTION__);
            // no need to wait if the value is already there.
            return !std::holds_alternative<std::monostate>(dest->value);
        }

        auto await_suspend(std::coroutine_handle<> handle) {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, handle.address());

            // this awaiter might be moved around a bit, but co_await should only ever be called
            // on the instance with the value.
            if(dest == nullptr) {
                throw doca_exception { DOCA_ERROR_UNEXPECTED };
            }

            if(std::holds_alternative<std::monostate>(dest->value)) {
                // value not known yet -> remember waiting coroutine and suspend.
                // the callback will set the value and resume that coroutine.
                dest->coro_handle = handle;
                return true;
            } else {
                // value already known by the time the awaiter waits, no need to suspend.
                return false;
            }
        }

        auto await_resume() const noexcept {
            return std::visit(
                overload{
                    [](T &&val) -> T&& {
                        return std::move(val);
                    },
                    [](std::exception_ptr ex) -> T&& {
                        std::rethrow_exception(ex);
                    },
                    [](std::monostate) -> T&& {
                        throw doca_exception(DOCA_ERROR_UNEXPECTED);
                    }
                },
                std::move(dest->value)
            );
        }
    };
}