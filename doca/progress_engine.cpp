#include "progress_engine.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>

namespace doca {
    progress_engine::progress_engine() {
        doca_pe *pe;
        enforce_success(doca_pe_create(&pe));
        handle_.reset(pe);
        epoll_.add_event_source(notification_handle());
        epoll_.add_event_source(yield_counter_.eventfd());
    }

    progress_engine::~progress_engine() {
        logger->debug("~pe: {} contexts still running, stopping them.", connected_contexts_.size());

        connected_contexts_.stop_all();

        logger->debug("~pe: Waiting for contexts to stop...");

        while(!connected_contexts_.empty()) {
            auto trigger_fd = wait(10);
            process_trigger(trigger_fd);

            logger->debug("~pe: {} contexts still running.", connected_contexts_.size());
        }

        logger->debug("~pe: all contexts stopped.");
    }

    auto progress_engine::notification_handle() const -> doca_event_handle_t {
        doca_event_handle_t event_handle = doca_event_invalid_handle;
        enforce_success(doca_pe_get_notification_handle(handle_.get(), &event_handle));
        return event_handle;
    }

    auto progress_engine::request_notification() const -> void {
        enforce_success(doca_pe_request_notification(handle_.get()));
    }

    auto progress_engine::clear_notification() const -> void {
        // handle parameter not used in linux, according to doca sample doca_common/pe_event/pe_event_sample.c
        // in dev container 2.7.0
        enforce_success(doca_pe_clear_notification(handle_.get(), 0));
    }

    auto progress_engine::inflight_tasks() const -> std::size_t {
        std::size_t num;
        enforce_success(doca_pe_get_num_inflight_tasks(handle_.get(), &num));
        return num;
    }

    auto progress_engine::connect(context *ctx) -> void {
        enforce_success(doca_pe_connect_ctx(handle(), ctx->as_ctx()));
    }

    auto progress_engine::signal_stopped_child(context *ctx) -> void {
        connected_contexts_.remove_stopped_context(ctx);
    }

    auto progress_engine::wait(int timeout_ms) const -> int {
        request_notification();
        auto trigger = epoll_.wait(timeout_ms);
        clear_notification();
        return trigger;
    }

    auto progress_engine::process_trigger(int trigger_fd) -> void {
        auto timer_it = timeout_waiters_.find(trigger_fd);

        // timers take priority to ensure timely execution (as much as
        // possible, i.e. no guarantees but reasonable effort)
        if(timer_it != timeout_waiters_.end()) {
            auto waiter = timer_it->second.waiter;
            timeout_waiters_.erase(timer_it);
            waiter.resume();
            return;
        }

        while(doca_pe_progress(handle_.get()) > 0) {
            // do nothing; progress() calls the event handlers.
        }

        // yielders do not take priority.
        if(trigger_fd == yield_counter_.eventfd()) {
            auto triggers_left = yield_counter_.pop();

            while(triggers_left > 0 && !pending_yielders_.empty()) {
                auto coro = pending_yielders_.front();
                pending_yielders_.pop();
                coro.resume();
                --triggers_left;
            }
        }
    }

    auto progress_engine::main_loop() -> void {
        main_loop_while([this] {
            return
                !connected_contexts_.empty() ||
                !timeout_waiters_.empty() ||
                !pending_yielders_.empty();
        });
    }

    auto progress_engine::main_loop_while(std::function<bool()> condition) -> void {
        while(condition()) {
            auto trigger = wait();
            process_trigger(trigger);
        }
    }

    auto progress_engine::push_yielding_coroutine(std::coroutine_handle<> yielder) -> void {
        pending_yielders_.push(yielder);
        yield_counter_.increase();
    }

    auto progress_engine::push_waiting_coroutine(std::coroutine_handle<> waiter, std::chrono::microseconds delay) -> void {
        auto new_timer = duration_timer { delay };
        auto fd = new_timer.timerfd();

        timeout_waiters_.insert(std::make_pair(fd, detail::coro_timeout { std::move(new_timer), waiter }));
        epoll_.add_event_source(fd);
    }

    auto yield_awaitable::await_suspend(std::coroutine_handle<> yielder) const -> void {
        engine_->push_yielding_coroutine(yielder);
    }

    auto timeout_awaitable::await_suspend(std::coroutine_handle<> waiter) const -> void {
        engine_->push_waiting_coroutine(waiter, delay_);
    }

    auto progress_engine::submit_task(doca_task *task, coro::error_receptable *reportee, std::uint32_t submit_flags) -> void {
        doca_error_t err;
        
        do {
            err = doca_task_submit_ex(task, submit_flags);
        } while(err == DOCA_ERROR_AGAIN);

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            reportee->set_error(err);
        }
    }
}
