#pragma once

#include "context.hpp"
#include "epoll_handle.hpp"
#include "error.hpp"
#include "unique_handle.hpp"

#include <doca_pe.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace doca {
    class progress_engine;

    /**
     * RAII wrapper around a doca_pe (progress engine) handle. Manages its lifetime, can wait for
     * events with epoll.
     * 
     * This class is mainly intended to be used inside the event_thread class for background handling
     * completion events (because we assume that the completion events will mainly trigger the completion
     * of promise/future pairs), but synchronous polling is also possible.
     * 
     * IMPORTANT: According to the DOCA documentation, doca_pe (which this class wraps) is not threadsafe.
     * In particular, as I understand it, tasks need to be submitted in the same thread that handles the
     * completion events (or at least when the program is certain no concurrent completion events are
     * possible, e.g. when offloading tasks sequentially).
     */
    class progress_engine
    {
    public:
        progress_engine();
        ~progress_engine();

        auto stop() -> void;

        /**
         * @return the managed doca_pe handle
         */
        [[nodiscard]] auto handle() const { return handle_.handle(); }
        [[nodiscard]] auto inflight_tasks() const -> std::size_t;

        /**
         * Process events for completed tasks.
         * 
         * @return number of completed tasks that were handled
         */
        [[nodiscard]]
        auto progress() -> std::uint8_t;
        auto wait(int timeout_ms = -1) -> void;

        auto submit_task(doca_task *) -> void;

        template<std::derived_from<context> Context, typename... Args>
        auto create_context(Args&&... args) -> Context* {
            // make sure push_back will not throw an exception
            if(connected_contexts_.capacity() == connected_contexts_.size()) {
                auto double_capacity = std::max(std::size_t(8), connected_contexts_.capacity() * 2);
                connected_contexts_.reserve(double_capacity);
            }

            auto new_context = std::make_unique<Context>(std::forward<Args>(args)...);
            new_context->set_progress_engine(this);
            auto non_owning_ptr = new_context.get();

            enforce_success(connect(non_owning_ptr));
            
            auto err = doca_ctx_start(non_owning_ptr->as_ctx());

            if(
                err != DOCA_SUCCESS && 
                err != DOCA_ERROR_IN_PROGRESS
            ) {
                throw doca_exception(err);
            }

            connected_contexts_.push_back(std::move(new_context));

            return non_owning_ptr;
        }

        auto main_loop() -> void;
        auto main_loop_while(std::function<bool()> condition) -> void;

    private:
        friend class context;
        auto remove_stopped_context(context *ctx) -> void;

        [[nodiscard]] auto notification_handle() const -> doca_event_handle_t;
        auto request_notification() -> void;
        auto clear_notification() -> void;

        auto connect(context *ctx) -> doca_error_t;

        unique_handle<doca_pe> handle_ { doca_pe_destroy };
        epoll_handle epoll_;
        std::vector<std::unique_ptr<context>> connected_contexts_;
    };
}
