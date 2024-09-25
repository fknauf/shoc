#pragma once

#include "task_promise.hpp"
#include <doca/logger.hpp>

#include <coroutine>
#include <variant>
#include <type_traits>

namespace doca::coro {
    template<typename Result>
    class [[nodiscard]] lazy_task {
    public:
        using promise_type = task_promise<Result, lazy_task, std::suspend_always>;

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
}
