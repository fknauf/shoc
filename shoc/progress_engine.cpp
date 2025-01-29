#include "progress_engine.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/detached.hpp>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>

namespace shoc {
    progress_engine::progress_engine(
        progress_engine_config cfg,
        boost::cobalt::executor executor
    ):
        cfg_ { std::move(cfg) },
        executor_ { std::move(executor) },
        notify_backend_ { executor_ }
    {
        doca_pe *pe;
        enforce_success(doca_pe_create(&pe));
        handle_.reset(pe);

        notify_backend_.assign(notification_handle());
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

    auto progress_engine::run() -> boost::cobalt::promise<void> try {
        while(!connected_contexts_.empty()) {
            request_notification();
            co_await notify_backend_.async_wait(
                boost::asio::posix::descriptor::wait_read,
                boost::cobalt::use_op
            );
            clear_notification();
            while(doca_pe_progress(handle()) > 0) {
                // do nothing; progress() calls the event handlers.
            }
        }
    } catch(boost::system::system_error &ex) {
        logger->error("unexpected system error in DOCA event handle: {}", ex.code().message());
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
            delayed_resubmission(task, reportee, cfg_.resubmission_attempts, cfg_.resubmission_interval);
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
    ) -> boost::cobalt::detached {
        doca_error_t err;

        using steady_timer = boost::cobalt::use_op_t::as_default_on_t<boost::asio::steady_timer>;
        auto timer = steady_timer(executor_);

        do {
            timer.expires_after(interval);
            co_await timer.async_wait();
            err = doca_task_submit(task);
            --attempts;
        } while(err == DOCA_ERROR_AGAIN && attempts > 0);

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            reportee->set_error(err);
        }
    }
}
