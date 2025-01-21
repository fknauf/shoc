#pragma once

#include "common/status.hpp"
#include "context.hpp"
#include "coro/fiber.hpp"
#include "coro/value_awaitable.hpp"
#include "epoll_handle.hpp"
#include "error.hpp"
#include "event_sources.hpp"
#include "unique_handle.hpp"

#include <doca_pe.h>

#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace shoc {
    namespace detail {
        struct coro_timeout {
            duration_timer timer;
            std::coroutine_handle<> waiter;
        };
    }

    class yield_awaitable:
        public std::suspend_always
    {
    public:
        yield_awaitable(progress_engine *engine):
            engine_ { engine }
        {}

        auto await_suspend(std::coroutine_handle<> yielder) const -> void;

    private:
        progress_engine *engine_;
    };

    class timeout_awaitable:
        public std::suspend_always
    {
    public:
        timeout_awaitable(progress_engine *engine, std::chrono::microseconds delay):
            engine_ { engine },
            delay_ { delay }
        {}

        auto await_suspend(std::coroutine_handle<> waiter) const -> void;

    private:
        progress_engine *engine_;
        std::chrono::microseconds delay_;
    };

    struct progress_engine_config {
        std::uint32_t immediate_submission_attempts = 16;
        std::uint32_t resubmission_attempts = 16;
        std::chrono::microseconds resubmission_interval = std::chrono::milliseconds(1);
    };

    /**
     * RAII wrapper around a doca_pe (progress engine) handle. Manages its lifetime, can wait for
     * events with epoll.
     *
     * IMPORTANT: According to the DOCA documentation, doca_pe (which this class wraps) is not threadsafe.
     * In particular, as I understand it, tasks need to be submitted in the same thread that handles the
     * completion events (or at least when the program is certain no concurrent completion events are
     * possible, e.g. when offloading tasks sequentially).
     *
     * The progress engine acts as a parent for all contexts that are created through it and handles all
     * their events and their children's events. Upon destruction, the progress engine will stop all
     * dependent contexts and wait for their stoppage before winding down.
     */
    class progress_engine:
        public context_parent
    {
    public:
        friend class yield_awaitable;
        friend class timeout_awaitable;

        progress_engine(progress_engine_config cfg = {});
        ~progress_engine();

        auto stop() -> void;

        /**
         * @return the managed doca_pe handle
         */
        [[nodiscard]] auto handle() const { return handle_.get(); }
        [[nodiscard]] auto inflight_tasks() const -> std::size_t;

        template<std::derived_from<context_base> Context, typename... Args>
        auto create_context(Args&&... args) {
            logger->trace("pe create_context this = {}", static_cast<void*>(this));
            return connected_contexts_.create_context<Context>(this, std::forward<Args>(args)...);
        }

        /**
         * Main event-handling loop: Wait for events and process them until there are no more
         * active dependent contexts.
         */
        auto main_loop() -> void;

        /**
         * Main event-handling loop with a custom loop condition.
         */
        auto main_loop_while(std::function<bool()> condition) -> void;

        auto connect(context_base *ctx) -> void;
        auto signal_stopped_child(context_base *ctx) -> void override;

        [[nodiscard]]
        auto engine() -> progress_engine* override {
            return this;
        }

        [[nodiscard]]
        auto yield() {
            return yield_awaitable { this };
        }

        [[nodiscard]]
        auto timeout(std::chrono::microseconds delay) {
            return timeout_awaitable { this, delay };
        }

        auto submit_task(
            doca_task *task,
            coro::error_receptable *reportee
        ) -> void;

    private:
        [[nodiscard]] auto notification_handle() const -> doca_event_handle_t;
        auto request_notification() const -> void;
        auto clear_notification() const -> void;
        auto wait(int timeout_ms = -1) const -> int;

        auto push_yielding_coroutine(std::coroutine_handle<> yielder) -> void;
        auto push_waiting_coroutine(std::coroutine_handle<> waiter, std::chrono::microseconds delay) -> void;

        auto process_trigger(int trigger_fd) -> void;
        auto delayed_resubmission(
            doca_task *task,
            coro::error_receptable *reportee,
            std::uint32_t attempts,
            std::chrono::microseconds delay
        ) -> coro::fiber;

        unique_handle<doca_pe, doca_pe_destroy> handle_;
        progress_engine_config cfg_;

        event_counter yield_counter_;
        std::queue<std::coroutine_handle<>> pending_yielders_;
        std::unordered_map<int, detail::coro_timeout> timeout_waiters_;
        epoll_handle epoll_;
        dependent_contexts<context_base> connected_contexts_;
    };

    namespace detail {
        template<auto AllocInit, auto AsTask, typename AdditionalData, typename... Args>
        auto status_offload(
            progress_engine *engine,
            coro::status_awaitable<AdditionalData> result,
            Args&&... args
        ) {
            auto receptable = result.receptable_ptr();

            detail::deduce_as_task_arg_type_t<AsTask> *task;
            doca_data task_user_data = { .ptr = receptable };

            auto err = AllocInit(
                std::forward<Args>(args)...,
                task_user_data,
                &task
            );

            if(err != DOCA_SUCCESS) {
                receptable->set_error(err);
            } else {
                auto base_task = AsTask(task);
                engine->submit_task(base_task, receptable);
            }

            return result;
        }

        template<auto AllocInit, auto AsTask, typename... Args>
        auto plain_status_offload(
            progress_engine *engine,
            Args&&... args
        ) {
            return status_offload<AllocInit, AsTask>(engine, coro::status_awaitable<>::create_space(), std::forward<Args>(args)...);
        }
    }
}
