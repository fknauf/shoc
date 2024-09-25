#pragma once

#include <doca/logger.hpp>

#include <coroutine>
#include <variant>
#include <type_traits>

namespace doca::coro {
    template<typename Result = void>
    class lazy_task;

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
        struct promise_base {
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
                return std::suspend_always{};
            }

            auto final_suspend() noexcept {
                logger->trace("{}", __PRETTY_FUNCTION__);
                return final_awaitable{};
            }

            std::coroutine_handle<> continuation;

            promise_base()= default;
            promise_base(promise_base const&) = delete;
            promise_base(promise_base&&) = delete;
            promise_base &operator=(promise_base const &) = delete;
            promise_base &operator=(promise_base &&) = delete;
            ~promise_base() = default;
        };

        template<typename Result>
        struct promise final:
            public promise_base
        {
            using stored_type = std::remove_const_t<Result>;

            auto get_return_object() noexcept -> lazy_task<Result>;

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
                auto visitor = overload {
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

        template<>
        struct promise<void>:
            public promise_base
        {
            auto get_return_object() noexcept -> lazy_task<void>;

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

    template<typename Result>
    class [[nodiscard]] lazy_task {
    public:
        using promise_type = detail::promise<Result>;

        struct awaitable {
            auto await_ready() const noexcept {
                logger->trace("{}, coro = {}, ready = {}", __PRETTY_FUNCTION__, coro.address(), !coro || coro.done());
                return !coro || coro.done();
            }

            auto await_suspend(std::coroutine_handle<> caller) noexcept -> std::coroutine_handle<> {
                logger->trace("{}, caller = {}", __PRETTY_FUNCTION__, caller.address());
                coro.promise().continuation = caller;
                return coro;
            }

            auto await_resume() {
                logger->trace("{}", __PRETTY_FUNCTION__);
                return coro.promise().result();
            }

            std::coroutine_handle<promise_type> coro;
        };

        lazy_task() noexcept = default;
        explicit lazy_task(std::coroutine_handle<promise_type> handle):
            coroutine { handle }
        {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, handle.address());
        }

        ~lazy_task() {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, coroutine.address());
            destroy();
        }

        lazy_task(lazy_task const &) = delete;
        lazy_task(lazy_task &&other) noexcept:
            coroutine { std::exchange(other.coroutine, nullptr) }
        {
            logger->trace("{}", __PRETTY_FUNCTION__);
        }

        lazy_task &operator=(lazy_task const &) = delete;
        lazy_task &operator=(lazy_task &&other) {
            logger->trace("{}", __PRETTY_FUNCTION__);
            if(std::addressof(other) != this) {
                destroy();
                coroutine = std::exchange(other.coroutine, nullptr);
            }
        }

        auto operator co_await() const noexcept {
            logger->trace("{}", __PRETTY_FUNCTION__);
            return awaitable { coroutine };
        }

    private:
        auto destroy() {
            logger->trace("{}", __PRETTY_FUNCTION__);
            if(coroutine) {
                coroutine.destroy();
                coroutine = nullptr;
                return true;
            }
            return false;
        }

        std::coroutine_handle<promise_type> coroutine;
    };

    namespace detail {
        template<typename Result>
        inline auto promise<Result>::get_return_object() noexcept -> lazy_task<Result> {
            logger->trace("{}", __PRETTY_FUNCTION__);
            return lazy_task<Result> { std::coroutine_handle<promise>::from_promise(*this) };
        }

        inline auto promise<void>::get_return_object() noexcept -> lazy_task<void> {
            logger->trace("{}", __PRETTY_FUNCTION__);
            return lazy_task<void> { std::coroutine_handle<promise>::from_promise(*this) };
        }
    }
}
