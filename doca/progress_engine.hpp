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

        auto connect(context *ctx) -> void;
        auto signal_stopped_child(context *ctx) -> void override;
        auto engine() -> progress_engine* override {
            return this;
        }

    private:
        [[nodiscard]] auto notification_handle() const -> doca_event_handle_t;
        auto request_notification() -> void;
        auto clear_notification() -> void;

        unique_handle<doca_pe> handle_ { doca_pe_destroy };
        epoll_handle epoll_;
        dependent_contexts<context> connected_contexts_;
    };
}
