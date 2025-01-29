#include "progress_engine.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>

namespace shoc {
    progress_engine::progress_engine(
        executor_type executor,
        progress_engine_config cfg
    ):
        cfg_ { std::move(cfg) },
        strand_ { std::move(executor) },
        notify_backend_ { strand_ }
    {
        doca_pe *pe;
        enforce_success(doca_pe_create(&pe));
        handle_.reset(pe);

        notify_backend_.assign(notification_handle());
        renew_trigger();
    }

    progress_engine::~progress_engine() {
        notify_backend_.cancel();

        if(!connected_contexts_.empty()) {
            logger->error("attempted to destroy progress engine while attached contexts are still running");
            logger->debug("~pe: {} contexts still running, attempting to stop.", connected_contexts_.size());

            clear_notification();
            connected_contexts_.stop_all();
            while(doca_pe_progress(handle()) > 0) {
                // do nothing.
            }

            logger->debug("~pe: {} contexts still running, giving up.", connected_contexts_.size());

            // we could wait for pending DOCA events, but if a context is kept alive
            // by a fiber waiting for a different event that'll lead to deadlocks.
            // If we give up control of the 

            // epoll_handle epoll_;
            // epoll_.add_event_source(notification_handle());
            // do {
            //     request_notification();
            //     auto trigger_fd = epoll_.wait(10);
            //     process_trigger(std::errc::success);
            // } while(!connected_contexts_.empty());
        } else {
            logger->debug("~pe: all contexts stopped.");
        }
    }

    auto progress_engine::notification_handle() const -> doca_event_handle_t {
        doca_event_handle_t event_handle = doca_event_invalid_handle;
        enforce_success(doca_pe_get_notification_handle(handle(), &event_handle));
        return event_handle;
    }

    auto progress_engine::request_notification() const -> void {
        enforce_success(doca_pe_request_notification(handle()));
    }

    auto progress_engine::clear_notification() const -> void {
        // handle parameter not used in linux, according to doca sample doca_common/pe_event/pe_event_sample.c
        // in dev container 2.7.0
        enforce_success(doca_pe_clear_notification(handle(), 0));
    }

    auto progress_engine::inflight_tasks() const -> std::size_t {
        std::size_t num;
        enforce_success(doca_pe_get_num_inflight_tasks(handle(), &num));
        return num;
    }

    auto progress_engine::connect(context_base *ctx) -> void {
        enforce_success(doca_pe_connect_ctx(handle(), ctx->as_ctx()));
    }

    auto progress_engine::signal_stopped_child(context_base *ctx) -> void {
        connected_contexts_.remove_stopped_context(ctx);
    }

    auto progress_engine::renew_trigger() -> void {
        request_notification();

        notify_backend_.async_wait(
            asio::posix::descriptor::wait_read,
            [this](std::error_code ec) {
                process_trigger(ec);
            }
        );
    }

    auto progress_engine::process_trigger(std::error_code ec) -> void {
        if(ec) {
            logger->error("unexpected system error in DOCA event handle: {}", ec.message());
        }

        clear_notification();
        while(doca_pe_progress(handle()) > 0) {
            // do nothing; progress() calls the event handlers.
        }

        if(!connected_contexts_.empty()) {
            renew_trigger();
        }
    }

    auto progress_engine::spawn(asio::awaitable<void> fiber) -> void {
        asio::co_spawn(strand_, std::move(fiber), asio::detached); 
    }

    auto progress_engine::submit_task(
        doca_task *task,
        coro::error_receptable *reportee
    ) -> void {
        doca_error_t err;
        std::uint32_t attempts = 0;

        do {
            err = doca_task_submit(task);
            ++attempts;
        } while(err == DOCA_ERROR_AGAIN && attempts <= cfg_.immediate_submission_attempts);
        
        if(err == DOCA_ERROR_AGAIN) {
            asio::co_spawn(strand(), delayed_resubmission(task, reportee, cfg_.resubmission_attempts, cfg_.resubmission_interval), asio::detached);
        } else if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            reportee->set_error(err);
        }
    }

    auto progress_engine::delayed_resubmission(
        doca_task *task,
        coro::error_receptable *reportee,
        std::uint32_t attempts,
        std::chrono::microseconds interval
    ) -> asio::awaitable<void> {
        doca_error_t err;

        do {
            co_await timeout(interval);
            err = doca_task_submit(task);
            --attempts;
        } while(err == DOCA_ERROR_AGAIN && attempts > 0);

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            reportee->set_error(err);
        }
    }
}
