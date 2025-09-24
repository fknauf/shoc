#pragma once

#include "asio_descriptor.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "coro/value_awaitable.hpp"
#include "error.hpp"
#include "unique_handle.hpp"

#include <doca_pe.h>

#include <boost/asio/post.hpp>
#include <boost/cobalt/detached.hpp>
#include <boost/cobalt/task.hpp>
#include <boost/cobalt/this_thread.hpp>
#include <system_error>

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
    enum class polling_mode {
        /// Use epoll to wait for events and sleep when idle
        epoll,
        /// Do not wait for events, just busily check and recheck and recheck
        busy
    };

    /**
     * Configuration for the progress engine
     */
    struct progress_engine_config {
        /// How often to immediately reattempt task submission when doca_task_submit returns DOCA_ERROR_AGAIN
        std::uint32_t immediate_submission_attempts = 64;
        /// How often to reattempt task submission in regular intervals after the immediate resubmission attempts are exhausted
        std::uint32_t resubmission_attempts = 64;
        /// Delay between resubmission attempts
        std::chrono::microseconds resubmission_interval = std::chrono::milliseconds(1);
        /// How to wait for events, see polling_mode
        polling_mode polling = polling_mode::epoll;
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
        progress_engine(
            progress_engine_config cfg = {},
            boost::cobalt::executor executor = boost::cobalt::this_thread::get_executor()
        );
        ~progress_engine();

        auto stop() -> void;

        /**
         * @return the managed doca_pe handle
         */
        [[nodiscard]] auto handle() const { return handle_.get(); }
        [[nodiscard]] auto inflight_tasks() const -> std::size_t;

        auto connect(context_base *ctx) -> void;
        auto signal_stopped_child(context_base *ctx) -> void override;

        [[nodiscard]]
        auto engine() -> progress_engine* override {
            return this;
        }

        [[nodiscard]]
        auto yield() {
            return boost::asio::post(executor_, boost::cobalt::use_op);
        }

        auto submit_task(
            doca_task *task,
            coro::error_receptable *reportee
        ) -> void;

        auto run() -> boost::cobalt::task<void>;

        [[nodiscard]]
        auto active() const {
            return active_;
        }

        [[nodiscard]]
        auto executor() const {
            return executor_;
        }

    private:
        friend class progress_engine_lease;

        template<std::derived_from<context_base> Context, typename... Args>
        auto create_context(Args&&... args) {
            logger->trace("pe create_context this = {}", static_cast<void*>(this));
            return connected_contexts_.create_context<Context>(this, std::forward<Args>(args)...);
        }

        auto register_fiber() -> void;
        auto deregister_fiber() -> void;

        [[nodiscard]] auto notification_handle() const -> doca_event_handle_t;
        auto request_notification() const -> void;
        auto clear_notification() const -> void;

        /**
         * Some tasks can only be submitted when conditions are right, notably comch producer's
         * send task can only be offloaded when there's a remote consumer waiting for it. In this
         * case we might need to spin for a while to offload the task. Our strategy here is to
         * immediately retry a couple of times (as in the DOCA comch samples) as configured in the
         * progress_engine_config, then retry in regular intervals for another couple of times.
         *
         * delayed_submission is the background fiber that resubmits in regular intervals.
         *
         * @param task DOCA task to resubmit
         * @param reportee receptable to report errors to in case of failure
         * @param attempts how often to attempt resubmission
         * @param delay Delay between resubmission attempts
         * @param exec_tag Empty struct, tells boost.cobalt that the next argument is the executor
        *                  for the coroutine.
         * @param executor The Boost.Cobalt executor associated with the progress engine. Why this
         *                 needs to be explictly passed is a bit of a mystery to me, but at time of
         *                 writing, without it Boost.Cobalt fails to find the thread-local executor.
         */
        auto delayed_resubmission(
            doca_task *task,
            coro::error_receptable *reportee,
            std::uint32_t attempts,
            std::chrono::microseconds delay,
            boost::asio::executor_arg_t exec_tag,
            boost::cobalt::executor executor
        ) -> boost::cobalt::detached;

        unique_handle<doca_pe, doca_pe_destroy> handle_;
        progress_engine_config cfg_;

        boost::cobalt::executor executor_;
        asio_descriptor<boost::cobalt::executor> notifier_;
        dependent_contexts<context_base> connected_contexts_;
        int registered_fibers_ = 0;
        bool active_ = false;
    };

    /**
     * Lease on a progress engine, to be held by fibers for access to the progress engine and
     * to let the progress engine know that it is still in use.
     *
     * We expect each client fiber to hold a lease on the progress engine until it exits.
     * This way, the progress_engine::run() fiber continues working as long as there are client
     * fibers even if there are currently no active contexts.
     */
    class progress_engine_lease {
    public:
        progress_engine_lease(progress_engine *engine = nullptr):
            engine_ { engine }
        {
            if(engine_ != nullptr) {
                engine_->register_fiber();
            }
        }

        progress_engine_lease(progress_engine_lease const &other):
            progress_engine_lease { other.engine_ }
        {}

        progress_engine_lease(progress_engine_lease &&other):
            engine_ { std::exchange(other.engine_, nullptr) }
        {}

        progress_engine_lease &operator=(progress_engine_lease const &other) {
            if(engine_ != other.engine_) {
                clear();
                auto copy = progress_engine_lease(other);
                *this = std::move(copy);
            }

            return *this;
        }

        progress_engine_lease &operator=(progress_engine_lease &&other) {
            if(std::addressof(other) != this) {
                clear();
                engine_ = std::exchange(other.engine_, nullptr);
            }

            return *this;
        }

        ~progress_engine_lease() {
            clear();
        }
        
        explicit operator bool() const noexcept {
            return engine_ != nullptr;
        }

        auto clear() -> void {
            if(engine_ != nullptr) {
                engine_->deregister_fiber();
                engine_ = nullptr;
            }
        }

        [[nodiscard]] auto get() const { return engine_; }
        auto operator->() const { return get(); }
        auto &operator*() const { return *get(); }

        template<std::derived_from<context_base> Context, typename... Args>
        [[nodiscard]]
        auto create_context(Args&&... args) {
            return engine_->create_context<Context>(std::forward<Args>(args)...);
        }

        [[nodiscard]]
        auto yield() {
            return engine_->yield();
        }

    private:
        progress_engine *engine_;
    };

    namespace detail {
        template<auto AsTask>
        struct deduce_as_task_arg_type {};

        template<typename TaskType, doca_task *AsTask(TaskType*)>
        struct deduce_as_task_arg_type<AsTask> {
            using type = TaskType;
        };

        template<auto AsTask>
        using deduce_as_task_arg_type_t = typename deduce_as_task_arg_type<AsTask>::type;

        // Most DOCA tasks are created with associated user data (of type doca_data), always accepted
        // as the last argument before the output parameter that accepts the constructed task.
        // However, some don't accept these user data in their constructor, in which case the
        // user data needs to be attached to the task object in a second step. We can tell thes
        // two cases apart by their function signatures, which is what these two create_task_object
        // function templates do.
        template<
            auto AllocInit,
            auto AsTask,
            typename AdditionalData,
            typename... Args
        >
        [[nodiscard]] auto create_task_object(
            coro::status_receptable<AdditionalData> *receptable,
            detail::deduce_as_task_arg_type_t<AsTask> **task,
            Args&&... args
        ) -> doca_error_t 
            requires std::invocable<decltype(AllocInit), Args..., doca_data, detail::deduce_as_task_arg_type_t<AsTask> **>
        {
            doca_data task_user_data = { .ptr = receptable };
            return AllocInit(std::forward<Args>(args)..., task_user_data, task);
        }

        template<
            auto AllocInit,
            auto AsTask,
            typename AdditionalData,
            typename... Args
        >
        [[nodiscard]] auto create_task_object(
            coro::status_receptable<AdditionalData> *receptable,
            detail::deduce_as_task_arg_type_t<AsTask> **task,
            Args&&... args
        ) -> doca_error_t 
            requires std::invocable<decltype(AllocInit), Args..., detail::deduce_as_task_arg_type_t<AsTask> **>
        {
            auto err = AllocInit(std::forward<Args>(args)..., task);

            if(err == DOCA_SUCCESS) {
                auto base_task = AsTask(*task);
                doca_data task_user_data = { .ptr = receptable };
                doca_task_set_user_data(base_task, task_user_data);
            }

            return err;
        }

        /**
         * Task offloading for the most common case, where the offloading awaitable returns a
         * status code upon co_await and the actual result is typically a side effect in a memory
         * buffer. There is support for additional side-effect output data such as immediate_data
         * in an RDMA receive task.
         *
         * @param AllocInit DOCA function that allocates a task object for the desired task type
         * @param AsTask DOCA function that casts the concrete task object to doca_task*
         * @param engine progress engine to which the task will be submitted. This has to be the
         *               same to which the context in which the task is allocated is connected,
         * @param args Arguments passed to AllocInit, generally consists of the context, memory
         *             buffers and task-specific data. args does not include the user data that
         *             will be attached to the task; that is handled internally.
         */
        template<
            auto AllocInit,
            auto AsTask,
            typename AdditionalData,
            typename... Args
        >
        [[nodiscard]] auto status_offload(
            progress_engine *engine,
            coro::status_awaitable<AdditionalData> result,
            Args&&... args
        ) {
            auto receptable = result.receptable_ptr();
            detail::deduce_as_task_arg_type_t<AsTask> *task;

            auto err = create_task_object<AllocInit, AsTask>(receptable, &task, std::forward<Args>(args)...);

            if(err != DOCA_SUCCESS) {
                receptable->set_error(err);
            } else {
                auto base_task = AsTask(task);
                engine->submit_task(base_task, receptable);
            }

            return result;
        }

        /**
         * status_offload without support for additional side-effect output data (the most common case)
         */
        template<auto AllocInit, auto AsTask, typename... Args>
        [[nodiscard]] auto plain_status_offload(
            progress_engine *engine,
            Args&&... args
        ) {
            return status_offload<AllocInit, AsTask>(engine, coro::status_awaitable<>::create_space(), std::forward<Args>(args)...);
        }
    }

    /**
     * Plain-vanilla task completion callback template
     *
     * Most offloaded tasks report only a status code and have the actual return data as
     * side effects in memory buffers that are attached to the task. In that case, completion
     * callbacks all do the same thing, so we've extracted it here.
     *
     * @param AsTask function to convert the actual task type to doca_task*
     * @param task task object as task-specific DOCA type
     * @param task_user_data User data attached to the task, i.e. a pointer to the status receptable
     * @param ctx_user_data User data attached to the context, i.e. a pointer to the SHOC context object
     */
    template<auto AsTask, bool deferred = false>
    auto plain_status_callback(
        detail::deduce_as_task_arg_type_t<AsTask> *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        assert(task_user_data.ptr != nullptr);

        auto dest = static_cast<typename coro::status_awaitable<>::payload_type*>(task_user_data.ptr);
        auto base_task = AsTask(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        dest->set_value(std::move(status));

        if(deferred) {
            auto ctx = static_cast<context_base*>(ctx_user_data.ptr);
            boost::asio::post(
                ctx->engine()->executor(),
                [dest] { dest->resume(); }
            );
        } else {
            dest->resume();
        }
    }
}
