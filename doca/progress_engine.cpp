#include "progress_engine.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace doca {
    progress_engine::progress_engine()
    {
        doca_pe *pe;
        enforce_success(doca_pe_create(&pe));
        handle_.reset(pe);
        epoll_.add_event_source(notification_handle());
    }

    progress_engine::~progress_engine() {
        logger->debug("~pe: {} contexts still running, stopping them.", connected_contexts_.size());

        connected_contexts_.stop_all();

        logger->debug("~pe: Waiting for contexts to stop...");

        while(!connected_contexts_.empty()) {
            request_notification();
            wait(10);
            clear_notification();
            while(progress() > 0) {
                // do nothing
            }

            logger->debug("~pe: {} contexts still running.", connected_contexts_.size());
        }
    }

    auto progress_engine::notification_handle() const -> doca_event_handle_t {
        doca_event_handle_t event_handle = doca_event_invalid_handle;
        enforce_success(doca_pe_get_notification_handle(handle_.handle(), &event_handle));
        return event_handle;
    }

    auto progress_engine::request_notification() -> void {
        enforce_success(doca_pe_request_notification(handle_.handle()));
    }

    auto progress_engine::clear_notification() -> void {
        // handle parameter not used in linux, according to doca sample doca_common/pe_event/pe_event_sample.c
        // in dev container 2.7.0
        enforce_success(doca_pe_clear_notification(handle_.handle(), 0));
    }

    auto progress_engine::progress() -> std::uint8_t {
        return doca_pe_progress(handle_.handle());
    }

    auto progress_engine::wait(int timeout_ms) -> void {
        request_notification();
        epoll_.wait(timeout_ms);
        clear_notification();
    }

    auto progress_engine::inflight_tasks() const -> std::size_t {
        std::size_t num;

        enforce_success(doca_pe_get_num_inflight_tasks(handle_.handle(), &num));

        return num;
    }

    auto progress_engine::submit_task(doca_task *task) -> void {
        using namespace std::chrono_literals;

        doca_error_t status;
        
        do {
            status = doca_task_submit(task);
            if(status == DOCA_ERROR_AGAIN) {
                logger->debug("failed to queue task, retrying...");
                std::this_thread::sleep_for(100ms);
            }
        } while(status == DOCA_ERROR_AGAIN);

        if(status != DOCA_SUCCESS) {
            doca_task_free(task);
            throw doca_exception(status);
        }
    }

    auto progress_engine::connect(context *ctx) -> void {
        enforce_success(doca_pe_connect_ctx(handle(), ctx->as_ctx()));
    }

    auto progress_engine::signal_stopped_child(context *ctx) -> void {
        connected_contexts_.remove_stopped_context(ctx);
    }

    auto progress_engine::main_loop() -> void {
        main_loop_while([this] {
            return !connected_contexts_.empty();
        });
    }

    auto progress_engine::main_loop_while(std::function<bool()> condition) -> void {
        while(condition()) {
            request_notification();
            wait();
            clear_notification();
            while(progress() > 0) {
                // do nothing
            }
        }
    }
}
