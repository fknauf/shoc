#pragma once

#include "error_receptable.hpp"
#include "overload.hpp"

#include <doca/error.hpp>
#include <doca/logger.hpp>

#include <cassert>
#include <coroutine>
#include <memory>
#include <utility>
#include <variant>

namespace doca::coro {
    /**
     * Meeting point for waiting coroutines and result-providing event callbacks.
     *
     * Callbacks can supply their results here before resuming the stored coroutine.
     * When a coroutine is suspended waiting for this result, a handle to it will be
     * stored here.
     *
     * used by value_awaitable.
     */
    template<typename T>
    struct value_receptable:
        public error_receptable
    {
        std::variant<std::monostate, std::exception_ptr, T> value;
        std::coroutine_handle<> coro_handle;

        value_receptable() = default;
        value_receptable(T &&val):
            value { std::move(val) }
        {}
        value_receptable(std::exception_ptr ex):
            value { ex }
        {}

        template<typename... Args>
        auto emplace_value(Args &&...args) {
            value.template emplace<T>(std::forward<Args>(args)...);
        }

        auto set_exception(std::exception_ptr ex) -> void override {
            value = ex;
        }

        auto set_error(doca_error_t err) -> void override {
            value = std::make_exception_ptr(doca_exception { err });
        }

        auto resume() {
            if(coro_handle) {
                coro_handle.resume();
            }
        }
    };

    /**
     * Awaitable that allows a single coroutine to wait for a single value.
     */
    template<typename T>
    struct [[nodiscard]] value_awaitable {
        using payload_type = value_receptable<T>;

        std::unique_ptr<payload_type> dest;

        value_awaitable() = default;

        value_awaitable(std::unique_ptr<payload_type> &&dest):
            dest { std::move(dest) }
        {}

        /**
         * Create a value_awaitable referencing an empty value_receptable.
         */
        static auto create_space() {
            return value_awaitable { std::make_unique<payload_type>() } ;
        }

        /**
         * create a value_awaitable from an existing value (so that coroutines will not have to wait)
         */
        static auto from_value(T &&val) {
            return value_awaitable(std::make_unique<payload_type>(std::move(val)));
        }

        /**
         * Create a value_awaitable that will throw an exception upon value retrieval
         */
        static auto from_exception(std::exception_ptr ex) {
            return value_awaitable(std::make_unique<payload_type>(ex));
        }

        /**
         * Create a value_awaitable that will throw a doca_exception with the supplied error code
         */
        static auto from_error(doca_error_t err) {
            return from_exception(std::make_exception_ptr(doca_exception { err }));
        }

        auto await_ready() const noexcept -> bool {
            // no need to wait if the value is already there.
            return !std::holds_alternative<std::monostate>(dest->value);
        }

        auto await_suspend(std::coroutine_handle<> handle) {
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

        [[nodiscard]]
        auto await_resume() const {
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
