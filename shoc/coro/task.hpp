#pragma once

#include "task_promise.hpp"

#include <coroutine>

namespace shoc::coro {
    template<typename Result, bool lazy>
    class [[nodiscard]] task {
    public:
        using promise_type = task_promise<Result, task, std::conditional_t<lazy, std::suspend_always, std::suspend_never>>;

        struct awaitable {
            auto await_ready() const noexcept {
                logger->trace("{}, coro = {}, ready = {}", __PRETTY_FUNCTION__, coro.address(), !coro || coro.done());
                return !coro || coro.done();
            }

            auto await_suspend(std::coroutine_handle<> caller) noexcept {
                logger->trace("{}, caller = {}", __PRETTY_FUNCTION__, caller.address());
                coro.promise().continuation = caller;

                if constexpr (lazy) {
                    return coro;
                }
            }

            auto await_resume() {
                logger->trace("{}", __PRETTY_FUNCTION__);
                return coro.promise().result();
            }

            std::coroutine_handle<promise_type> coro;
        };

        task() noexcept = default;
        explicit task(std::coroutine_handle<promise_type> handle):
            coroutine { handle }
        {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, handle.address());
        }

        ~task() {
            logger->trace("{}, handle = {}", __PRETTY_FUNCTION__, coroutine.address());
            destroy();
        }

        task(task const &) = delete;
        task(task &&other) noexcept:
            coroutine { std::exchange(other.coroutine, nullptr) }
        {
            logger->trace("{}", __PRETTY_FUNCTION__);
        }

        task &operator=(task const &) = delete;
        task &operator=(task &&other) {
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

    template<typename Result> using lazy_task = task<Result, true>;
    template<typename Result> using eager_task = task<Result, false>;
}
