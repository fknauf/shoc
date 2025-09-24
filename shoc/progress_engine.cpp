#include "progress_engine.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/cobalt/detached.hpp>
#include <boost/cobalt/run.hpp>

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
        executor_ { executor },
        notifier_ { std::move(executor) }
    {
        doca_pe *pe;
        enforce_success(doca_pe_create(&pe));
        handle_.reset(pe);

        notifier_.assign(notification_handle());
    }

    progress_engine::~progress_engine() {
        if(!connected_contexts_.empty()) {
            logger->error("attempted to destroy progress engine while attached contexts are still running");
            logger->debug("~pe: {} contexts still running, attempting to stop.", connected_contexts_.size());

            clear_notification();
            connected_contexts_.stop_all();
            while(doca_pe_progress(handle()) > 0) {
                // do nothing.
            }

            logger->debug("~pe: {} contexts still running.", connected_contexts_.size());
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

    auto progress_engine::run() -> boost::cobalt::task<void> {
        active_ = true;

        while(registered_fibers_ > 0 || !connected_contexts_.empty()) {
            if(cfg_.polling == polling_mode::busy) {
                std::uint8_t num_popped;

                do {
                    num_popped = doca_pe_progress(handle());
                    //logger->trace("processed {} doca tasks", num_popped);
                } while(num_popped > 0);
            } else {
                request_notification();
                logger->trace("progress engine: waiting for notification");
                auto [ ec ] = co_await notifier_.async_wait(
                    boost::asio::posix::descriptor::wait_read,
                    boost::asio::as_tuple(boost::cobalt::use_op)
                );
                logger->trace("progress engine: got notification");
                clear_notification();

                while(doca_pe_progress(handle()) > 0) {
                    // intentionally left blank
                }

                if(ec == boost::asio::error::operation_aborted) {
                    connected_contexts_.stop_all();
                    while(doca_pe_progress(handle()) > 0) { }
                    break;
                } else if(ec) {
                    logger->error("unexpected system error in DOCA event handle: {}", ec.message());
                    break;
                }
            }

            // yield before re-checking fiber states is necessary because some
            // fiber resumptions are deferred and handled by the cobalt engine.
            // this happens because some operations complain when they are
            // done directly from a callback (esp. eth_txq)
            co_await yield();
        }

        active_ = false;
    }

    auto progress_engine::register_fiber() -> void {
        ++registered_fibers_;
    }

    auto progress_engine::deregister_fiber() -> void {
        if(registered_fibers_ <= 0) {
            logger->error("deregistered more fibers than were registered");
            return;
        }

        --registered_fibers_;

        if(registered_fibers_ == 0 && connected_contexts_.empty()) {
            notifier_.cancel();
        }
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
            delayed_resubmission(task, reportee, cfg_.resubmission_attempts, cfg_.resubmission_interval, {}, executor_);
        } else if(err != DOCA_SUCCESS) {
            logger->debug("failed submitting: {}", doca_error_get_descr(err));
            doca_task_free(task);
            reportee->set_error(err);
        }
    }

    auto progress_engine::delayed_resubmission(
        doca_task *task,
        coro::error_receptable *reportee,
        std::uint32_t attempts,
        std::chrono::microseconds interval,
        boost::asio::executor_arg_t,
        boost::cobalt::executor executor
    ) -> boost::cobalt::detached {
        using steady_timer = boost::cobalt::use_op_t::as_default_on_t<boost::asio::steady_timer>;

        doca_error_t err;
        auto timer = steady_timer(std::move(executor));

        do {
            timer.expires_after(interval);
            co_await timer.async_wait();

            logger->trace("resubmitting task after delay of {} us, {} attempts left", interval.count(), attempts);

            err = doca_task_submit(task);
            --attempts;
        } while(err == DOCA_ERROR_AGAIN && attempts > 0 && !connected_contexts_.empty());

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            reportee->set_error(err);
        }
    }
}
