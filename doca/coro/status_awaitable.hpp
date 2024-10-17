#pragma once

#include "error_receptable.hpp"
#include "overload.hpp"

#include <doca/error.hpp>

#include <concepts>
#include <coroutine>
#include <memory>
#include <variant>

namespace doca::coro {
    class status_receptable:
        public error_receptable
    {
    public:
        status_receptable() = default;
        status_receptable(doca_error_t status):
            value_ { status }
        {}
        
        status_receptable(std::exception_ptr ex):
            value_ { ex }
        {}

        auto set_exception(std::exception_ptr ex) -> void override {
            value_ = ex;
        }

        auto set_error(doca_error_t status) -> void override {
            emplace_value(status);
        }

        auto emplace_value(doca_error_t status) -> void {
            value_ = status;
        }

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

        auto get_value() const {
            return std::visit(
                overload {
                    [](std::monostate) -> doca_error_t {
                        throw doca_exception(DOCA_ERROR_UNEXPECTED);
                    },
                    [](std::exception_ptr ex) -> doca_error_t {
                        std::rethrow_exception(ex);
                    },
                    [](doca_error_t status) -> doca_error_t {
                        return status;
                    }
                },
                value_
            );
        }

    private:
        std::variant<
            std::monostate,
            doca_error_t,
            std::exception_ptr
        > value_;

        std::coroutine_handle<> waiter_;
    };

    class status_awaitable {
    public:
        using payload_type = status_receptable;

        status_awaitable() = default;

        status_awaitable(std::unique_ptr<payload_type> &&dest):
            dest_ { std::move(dest) }
        {}

        static auto create_space() {
            return status_awaitable { std::make_unique<payload_type>() };
        }

        static auto from_value(doca_error_t status) {
            return status_awaitable { std::make_unique<payload_type>(status) };
        }

        static auto from_exception(std::exception_ptr ex) {
            return status_awaitable { std::make_unique<payload_type>(ex) };
        }

        auto receptable_ptr() {
            return dest_.get();
        }

        auto await_ready() const noexcept -> bool {
            return dest_ && dest_->has_value();
        }

        auto await_suspend(std::coroutine_handle<> handle) const -> void {
            if(dest_ == nullptr) {
                throw doca_exception(DOCA_ERROR_UNEXPECTED);
            }

            dest_->set_waiter(handle);
        }

        [[nodiscard]]
        auto await_resume() const {
            return dest_->get_value();
        }

    private:
        std::unique_ptr<payload_type> dest_;
    };
}