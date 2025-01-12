#pragma once

#include "error_receptable.hpp"

#include <shoc/common/overload.hpp>
#include <shoc/error.hpp>
#include <shoc/logger.hpp>

#include <cassert>
#include <coroutine>
#include <memory>
#include <utility>
#include <variant>

namespace shoc::coro {
    /**
     * Meeting point for waiting coroutines and result-providing event callbacks. Generally
     * a pointer to this will be provided as task_user_data to offloaded tasks.
     *
     * Callbacks can supply their results here before resuming the stored coroutine.
     * When a coroutine is suspended waiting for this result, a handle to it will be
     * stored here.
     *
     * used by value_awaitable.
     */    template<typename Payload>
    class value_receptable:
        public error_receptable
    {
    public:
        value_receptable() = default;
        value_receptable(Payload &&value):
            value_ { std::move(value) }
        {}
        value_receptable(std::exception_ptr ex):
            value_ { ex }
        {}

        auto set_exception(std::exception_ptr ex) -> void override {
            value_ = ex;
        }

        auto set_error(doca_error_t status) -> void override {
            set_exception(std::make_exception_ptr(doca_exception(status)));
        }

        auto set_value(Payload &&payload) -> void {
            value_ = std::move(payload);
        }

        template<typename... Args>
        auto emplace_value(Args&&... args) {
            value_.template emplace<Payload>(std::forward<Args>(args)...);
        }

        [[nodiscard]]
        auto has_value() -> bool {
            return !std::holds_alternative<std::monostate>(value_);
        }

        auto set_waiter(std::coroutine_handle<> waiter) -> void {
            if(waiter_ != nullptr) {
                throw doca_exception(DOCA_ERROR_IN_USE);
            }

            waiter_ = waiter;
        }

        auto resume() const {
            if(waiter_) {
                waiter_.resume();
            }
        }

        [[nodiscard]]
        auto value() -> Payload&& {
            return std::visit(
                overload {
                    [](std::monostate) -> Payload&& {
                        throw doca_exception(DOCA_ERROR_UNEXPECTED);
                    },
                    [](std::exception_ptr ex) -> Payload&& {
                        std::rethrow_exception(ex);
                    },
                    [](Payload &&val) -> Payload&& {
                        return std::move(val);
                    }
                },
                std::move(value_)
            );
        }

    private:
        std::variant<std::monostate, std::exception_ptr, Payload> value_;
        std::coroutine_handle<> waiter_;
    };

    /**
     * Awaitable that allows a single coroutine to wait for a single value.
     *
     * Must be kept alive long enough for the corresponding callback to fill the
     * receptable. Normally this can be done by co_await-ing it.
     *
     * Most member functions here are intended for internal use; the user is only
     * expected to use co_await, move assignment, move construction, and default
     * construction (in which case co_awaiting the empty awaitable will throw an
     * exception). This is meant to enable offloading multiple tasks before co_await-ing
     * the first of them.
     */
    template<typename T>
    class [[nodiscard]] value_awaitable {
    public:
        using payload_type = value_receptable<T>;

        value_awaitable() = default;

        value_awaitable(std::unique_ptr<payload_type> &&dest):
            dest_ { std::move(dest) }
        {}

        /**
         * Non-owning pointer to the receptable.
         */
        auto receptable_ptr() {
            return dest_.get();
        }

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
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);

            // no need to wait if the value is already there.
            return dest_->has_value();
        }

        auto await_suspend(std::coroutine_handle<> handle) {
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);
            dest_->set_waiter(handle);
        }

        [[nodiscard]]
        auto await_resume() const {
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);
            return dest_->value();
        }

    private:
        std::unique_ptr<payload_type> dest_;
    };
}
