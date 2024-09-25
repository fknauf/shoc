#pragma once

#include <doca/logger.hpp>

#include <coroutine>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace doca::coro {
    namespace detail {
        // for convenient std::variant visiting
        template<typename... Fs> struct overload : Fs... { using Fs::operator()...; };
        template<typename... Fs> overload(Fs...) -> overload<Fs...>;

        /**
         * base class for coroutine promise types. We will have to specialize for promise_types that
         * return a value on the one hand and those that return void on the other, and this contains
         * the common part.
         *
         * This common part concerns two main features of a task coroutine:
         *
         * 1. it is awaitable by other coroutines
         * 2. return values and/or thrown exceptions are propagated to the waiter
         */
        template<typename InitialSuspend>
        struct task_promise_base {
            /**
             * coroutine lifecycle management: when a lazy_task<...> coroutine returns, it'll suspend and keep
             * the coroutine (and more importantly its promise) alive for us to extract the return value
             * and/or stored exception.
             *
             * the final_awaitable facilitates this.
             */
            struct final_awaitable {
                auto await_ready() const noexcept {
                    logger->trace("{}", __PRETTY_FUNCTION__);
                    return false;
                }

                template<typename Promise>
                auto await_suspend(std::coroutine_handle<Promise> coroutine) noexcept -> std::coroutine_handle<> {
                    logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, coroutine.address());
                    if(coroutine.promise().continuation) {
                        return coroutine.promise().continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }

                auto await_resume() noexcept -> void {
                    logger->trace("{}", __PRETTY_FUNCTION__);
                }
            };

            auto initial_suspend() noexcept {
                logger->trace("{}", __PRETTY_FUNCTION__);
                return InitialSuspend{};
            }

            auto final_suspend() noexcept {
                logger->trace("{}", __PRETTY_FUNCTION__);
                return final_awaitable{};
            }

            std::coroutine_handle<> continuation;

            task_promise_base()= default;
            task_promise_base(task_promise_base const&) = delete;
            task_promise_base(task_promise_base&&) = delete;
            task_promise_base &operator=(task_promise_base const &) = delete;
            task_promise_base &operator=(task_promise_base &&) = delete;
            ~task_promise_base() = default;
        };
    }

    template<
        typename Result,
        typename Task,
        typename InitialSuspend
    >
    struct task_promise final:
        public detail::task_promise_base<InitialSuspend>
    {
        using stored_type = std::remove_const_t<Result>;

        auto get_return_object() noexcept -> Task {
            return Task { std::coroutine_handle<task_promise>::from_promise(*this) };
        }

        template<std::convertible_to<Result> Value>
        auto return_value(Value &&val) {
            logger->trace("{}", __PRETTY_FUNCTION__);
            storage_.template emplace<stored_type>(std::move(val));
        }

        auto unhandled_exception() noexcept {
            logger->trace("{}", __PRETTY_FUNCTION__);
            storage_ = std::current_exception();
        }

        auto result() {
            logger->trace("{}", __PRETTY_FUNCTION__);
            auto visitor = detail::overload {
                [](stored_type &val) -> stored_type { return val; },
                [](std::exception_ptr ex) -> stored_type { std::rethrow_exception(ex); },
                [](std::monostate)  -> stored_type {
                    throw std::runtime_error("coroutine return value was not set. Was the coroutine executed?");
                }
            };

            return std::visit(visitor, storage_);
        }

    private:
        std::variant<std::monostate, stored_type, std::exception_ptr> storage_;
    };

    template<
        typename Task,
        typename InitialSuspend
    >
    struct task_promise<void, Task, InitialSuspend>:
        public detail::task_promise_base<InitialSuspend>
    {
        auto get_return_object() noexcept -> Task {
            return Task { std::coroutine_handle<task_promise>::from_promise(*this) };
        }

        auto return_void() noexcept {
            logger->trace("{}", __PRETTY_FUNCTION__);
        }

        auto unhandled_exception() noexcept {
            logger->trace("{}", __PRETTY_FUNCTION__);
            ex_ = std::current_exception();
        }

        auto result() {
            logger->trace("{}", __PRETTY_FUNCTION__);
            if(ex_) {
                std::rethrow_exception(ex_);
            }
        }

    private:
        std::exception_ptr ex_;
    };
}
