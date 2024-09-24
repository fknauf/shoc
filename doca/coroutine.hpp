#pragma once

#include "logger.hpp"

#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace doca::coro {
    struct basic_coroutine {
        struct promise_type {
            auto get_return_object() const noexcept { return basic_coroutine{}; }
            auto unhandled_exception() const noexcept {}
            auto return_void() const noexcept {}
            auto initial_suspend() const noexcept { return std::suspend_never{}; }
            auto final_suspend() const noexcept { return std::suspend_never{}; }
        };
    };

    template<typename Result = void>
    class task;

    namespace detail {
        template<typename... Fs> struct overload : Fs... { using Fs::operator()...; };
        template<typename... Fs> overload(Fs...) -> overload<Fs...>;

        struct promise_base {
            struct final_awaitable {
                auto await_ready() const noexcept {
                    logger->trace(__PRETTY_FUNCTION__);
                    return false;
                }

                template<typename Promise>
                auto await_suspend(std::coroutine_handle<Promise> coroutine) noexcept -> std::coroutine_handle<> {
                    logger->trace(__PRETTY_FUNCTION__);

                    if(coroutine.promise().continuation) {
                        return coroutine.promise().continuation;
                    } else {
                        return std::noop_coroutine();
                    }
                }

                auto await_resume() noexcept -> void {
                    logger->trace(__PRETTY_FUNCTION__);
                }
            };

            auto initial_suspend() noexcept { 
                logger->trace(__PRETTY_FUNCTION__);
                return std::suspend_always{};
            }

            auto final_suspend() noexcept {
                logger->trace(__PRETTY_FUNCTION__);
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

            auto get_return_object() noexcept -> task<Result>;

            template<std::convertible_to<Result> Value>
            auto return_value(Value &&val) {
                logger->trace(__PRETTY_FUNCTION__);
                storage_.template emplace<stored_type>(std::move(val));
            }

            auto unhandled_exception() noexcept {
                logger->trace(__PRETTY_FUNCTION__);
                storage_ = std::current_exception();
            }

            auto result() {
                logger->trace(__PRETTY_FUNCTION__);
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
            auto get_return_object() noexcept -> task<void>;

            auto return_void() noexcept {
                logger->trace(__PRETTY_FUNCTION__);
            }

            auto unhandled_exception() noexcept {
                logger->trace(__PRETTY_FUNCTION__);
                ex_ = std::current_exception();
            }

            auto result() {
                logger->trace(__PRETTY_FUNCTION__);
                if(ex_) {
                    std::rethrow_exception(ex_);
                }
            }

        private:
            std::exception_ptr ex_;
        };
    }

    template<typename Result>
    class [[nodiscard]] task {
    public:
        using promise_type = detail::promise<Result>;

        struct awaitable {
            auto await_ready() const noexcept {
                logger->trace(__PRETTY_FUNCTION__);
                return !coro || coro.done();
            }

            auto await_suspend(std::coroutine_handle<> caller) noexcept -> std::coroutine_handle<> {
                logger->trace(__PRETTY_FUNCTION__);
                coro.promise().continuation = caller;
                return coro;
            }

            auto await_resume() {
                logger->trace(__PRETTY_FUNCTION__);
                return coro.promise().result();
            }

            std::coroutine_handle<promise_type> coro;
        };

        task() noexcept = default;
        explicit task(std::coroutine_handle<promise_type> handle):
            coroutine { handle }
        {
            logger->trace(__PRETTY_FUNCTION__);
        }

        ~task() {
            logger->trace(__PRETTY_FUNCTION__);
            destroy();
        }

        task(task const &) = delete;
        task(task &&other) noexcept:
            coroutine { std::exchange(other.coroutine, nullptr) }
        {
            logger->trace(__PRETTY_FUNCTION__);
        }

        task &operator=(task const &) = delete;
        task &operator=(task &&other) {
            logger->trace(__PRETTY_FUNCTION__);
            if(std::addressof(other) != this) {
                destroy();
                coroutine = std::exchange(other.coroutine, nullptr);
            }
        }

        auto operator co_await() const noexcept {
            logger->trace(__PRETTY_FUNCTION__);
            return awaitable { coroutine };
        }

    private:
        auto destroy() {
            logger->trace(__PRETTY_FUNCTION__);
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
        inline auto promise<Result>::get_return_object() noexcept -> task<Result> {
            logger->trace(__PRETTY_FUNCTION__);
            return task<Result> { std::coroutine_handle<promise>::from_promise(*this) };
        }

        inline auto promise<void>::get_return_object() noexcept -> task<void> {
            logger->trace(__PRETTY_FUNCTION__);
            return task<void> { std::coroutine_handle<promise>::from_promise(*this) };
        }
    }

    template<typename T, typename Promise = void>
    struct receptable {
        T value;
        std::coroutine_handle<Promise> coro_handle;

        ~receptable() {
            logger->trace("{}", __PRETTY_FUNCTION__);
        }
    };

    template<typename T, typename Promise = void>
    struct receptable_awaiter {
        receptable<T, Promise> *dest;

        ~receptable_awaiter() {
            logger->trace("{}", __PRETTY_FUNCTION__);
        }

        auto await_ready() const noexcept {
            logger->trace(__PRETTY_FUNCTION__);
            return false;
        }
        
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            logger->trace("{}: coro = {}", __PRETTY_FUNCTION__, handle.address());
            dest->coro_handle = handle;
        }

        auto await_resume() const noexcept {
            logger->trace(__PRETTY_FUNCTION__);        
        }
    };
}
