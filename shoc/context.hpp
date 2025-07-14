#pragma once

#include "error.hpp"
#include "logger.hpp"
#include "unique_handle.hpp"

#include <doca_ctx.h>

#include <algorithm>
#include <concepts>
#include <coroutine>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace shoc {
    class context_base;
    class progress_engine;

    /**
     * Interface for classes that have dependent context (e.g. the progress engine or other contexts)
     */
    class context_parent {
    public:
        virtual ~context_parent() = default;

        /**
         * Called by the stopping child when a child is stopped
         */
        virtual auto signal_stopped_child(context_base *stopped_child) -> void = 0;

        /**
         * handle to the common progress engine, so parent and child run on the same engine.
         */
        virtual auto engine() -> progress_engine* = 0;
    };

    /**
     * Awaitable for context state changes (to started or stopped). Jointly owns the waited-upon context
     * with its parent to keep it alive long enough for the awaitable to use in all circumstances (particularly
     * when co_await is delayed or the context destroyed synchronously) so as to avoid use-after-free.
     *
     * See C++20 coroutine docs for the meaning of the member functions.
     */
    class [[nodiscard]] context_state_awaitable
    {
    public:
        context_state_awaitable(std::shared_ptr<context_base> ctx, doca_ctx_states desired_state):
            ctx_(std::move(ctx)), desired_state_(desired_state)
        {
            logger->trace("context_state_awaitable ctx = {}, desired_state = {}", static_cast<void*>(ctx_.get()), static_cast<int>(desired_state_));
        }

        auto await_ready() const noexcept -> bool;
        auto await_resume() const noexcept {}

        auto await_suspend(std::coroutine_handle<> caller) noexcept -> void;

    private:
        std::shared_ptr<context_base> ctx_;
        doca_ctx_states desired_state_;
    };

    enum context_state {
        idle,
        running,
        stopping
    };

    /**
     * Base class for DOCA contexts. Modelled as a context_parent for ease of implementation, even
     * though not all contexts can have child contexts.
     *
     * Always constructed in a std::shared_ptr so context_state_awaitable can own it.
     */
    class context_base:
        public context_parent,
        public std::enable_shared_from_this<context_base>
    {
    public:
        /**
         * For internal use.
         */
        [[nodiscard]]
        auto start() -> context_state_awaitable;

        /**
         * stop this context (asynchronously)
         *
         * @return awaitable to co_await upon until the context is actually stopped
         */
        [[nodiscard]]
        virtual auto stop() -> context_state_awaitable;

        /**
         * For internal use.
         *
         * @return this context's DOCA handle as doca_ctx*, for use in some SDK functions.
         */
        [[nodiscard]]
        virtual auto as_ctx() const noexcept -> doca_ctx* = 0;

        /**
         * Get current context state (idle, starting, running, stopping)
         */
        [[nodiscard]]
        auto doca_state() const noexcept -> doca_ctx_states {
            return current_state_;
        }

        [[nodiscard]]
        auto state() const -> context_state;

        /**
         * Called by child contexts to signal that they have been stopped.
         */
        auto signal_stopped_child([[maybe_unused]] context_base *stopped_child) -> void override {}

        /**
         * @return the progress engine that handles the events for this context
         */
        [[nodiscard]]
        auto engine() -> progress_engine* override {
            return static_cast<context_base const*>(this)->engine();
        }

        [[nodiscard]]
        auto engine() const -> progress_engine* {
            return parent_->engine();
        }

        /**
         * @return the number of tasks that are in flight for this context
         */
        [[nodiscard]]
        auto inflight_tasks() const -> std::size_t;

    protected:
        /**
         * @param parent parent to signal when this context is stopped
         */
        context_base(context_parent *parent);

        context_base(context_base const &) = delete;
        context_base(context_base&&) = delete;
        context_base &operator=(context_base const &) = delete;
        context_base &operator=(context_base &&) = delete;

        [[nodiscard]]
        virtual auto preparing_stop() const noexcept -> bool {
            return false;
        }

        /**
         * Overridable state-changed handler. This is called from the state-change callback after the current state
         * is updated but before waiting coroutines are resumed and allows child contexts to update their state
         * in the way those waiting coroutines expect.
         */
        virtual auto state_changed(
            [[maybe_unused]] doca_ctx_states prev_state,
            [[maybe_unused]] doca_ctx_states next_state
        ) -> void {
        }

        auto connect_to_engine() -> void;

        template<std::derived_from<context_base> BaseContext>
        friend class dependent_contexts;
        friend class context_state_awaitable;

        context_parent *parent_ = nullptr;
        doca_ctx_states current_state_ = DOCA_CTX_STATE_IDLE;

        // coroutines waiting for start/stop state changes
        std::coroutine_handle<> coro_start_;
        std::coroutine_handle<> coro_stop_;
    };

    /**
     * Base class template for concrete context classes. Takes care of common context concerns such as the
     * signaling between child and parent contexts on state changes
     *
     * @param DocaHandle wrapped plain-DOCA context handle type
     * @param HandleDestroy function that destroys a DocaHandle
     * @param HandleAsCtx function that converts a pointer to DocaHandle to a doca_ctx*
     */
    template<
        typename DocaHandle,
        auto HandleDestroy,
        auto HandleAsCtx,
        bool HandleIsCreatedWithEngineConnection = false
    >
    class context:
        public context_base
    {
    public:
        /**
         * @return the underlying plain-DOCA context handle
         */
        [[nodiscard]]
        auto handle() const noexcept {
            return handle_.get();
        }

        /**
         * @return this context as doca_ctx*
         */
        [[nodiscard]]
        auto as_ctx() const noexcept -> doca_ctx* override {
            return HandleAsCtx(handle());
        }

    protected:
        context(
            context_parent *parent,
            DocaHandle *raw_handle
        ):
            context_base { parent },
            handle_ { raw_handle }
        {
            auto ctx = HandleAsCtx(handle_.get());
            doca_data ctx_user_data = { .ptr = this };
            enforce_success(doca_ctx_set_user_data(ctx, ctx_user_data));
            enforce_success(doca_ctx_set_state_changed_cb(ctx, &context::state_changed_callback));

            if(!HandleIsCreatedWithEngineConnection) {
                connect_to_engine();
            }
        }

        template<auto HandleCreate, typename... Args>
        static auto create_doca_handle(Args&&... args) {
            DocaHandle *raw_handle;
            enforce_success(HandleCreate(std::forward<Args>(args)..., &raw_handle));
            return raw_handle;
        }

        static auto state_changed_callback(
            doca_data user_data,
            [[maybe_unused]] doca_ctx *ctx,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void {
            auto obj = static_cast<context*>(user_data.ptr);
            assert(obj != nullptr);

            obj->current_state_ = next_state;

            try {
                obj->state_changed(prev_state, next_state);
            } catch(std::exception &e) {
                logger->error("state change event handler finished with error: {}", e.what());
            } catch(...) {
                logger->error("state change event handler finished with unknown error");
            }

            if(next_state == DOCA_CTX_STATE_RUNNING) {
                logger->debug("context started");

                auto coro = std::exchange(obj->coro_start_, nullptr);
                if(coro) {
                    coro.resume();
                }
            } else if(next_state == DOCA_CTX_STATE_IDLE) {
                logger->debug("context stopped");

                auto coro = std::exchange(obj->coro_stop_, nullptr);
                obj->handle_.reset(nullptr);
                // may delete obj if coro is nullptr and there's no coroutine holding on to
                // this context anymore, so we can't use it after this.
                obj->parent_->signal_stopped_child(obj);

                if(coro) {
                    coro.resume();
                }
            }
        }

        unique_handle<DocaHandle, HandleDestroy> handle_;
    };

    /**
     * Scoped context wrapper for automatic cleanup. Modelled after std::unique_ptr
     * with more limited functionality.
     *
     * Note that the referenced context object will survive the destructor of unique_scoped_context
     * unless it has been stopped before. The purpose of this wrapper is not to prevent a
     * memory leak on the context but to prevent it from never being stopped.
     */
    template<std::derived_from<context_base> ConcreteContext>
    class unique_scoped_context {
    public:
        ~unique_scoped_context() {
            clear();
        }

        unique_scoped_context() = default;
        unique_scoped_context(std::shared_ptr<ConcreteContext> ctx):
            ctx_ { std::move(ctx) }
        {}

        unique_scoped_context(unique_scoped_context const &) = delete;
        unique_scoped_context(unique_scoped_context &&other):
            ctx_ { std::exchange(other.ctx_, nullptr) }
        { }

        unique_scoped_context &operator=(unique_scoped_context const &) = delete;
        unique_scoped_context &operator=(unique_scoped_context &&other) {
            clear();
            other.ctx_ = std::exchange(other.ctx_, nullptr);
            return *this;
        }

        auto get() const noexcept {
            return ctx_.get();
        }

        auto operator->() const noexcept {
            return get();
        }

        auto &operator*() const noexcept {
            return *get();
        }

    private:
        auto clear() -> void {
            if(ctx_ != nullptr && ctx_->handle() != nullptr) {
                logger->trace("auto-stopping ctx {}", static_cast<void*>(get()));
                static_cast<void>(ctx_->stop());
                ctx_ = nullptr;
            }
        }

        std::shared_ptr<ConcreteContext> ctx_ = nullptr;
    };

    /**
     * reference-counted context handle for shared ownership between fibers.
     */
    template<std::derived_from<context_base> ConcreteContext>
    class shared_scoped_context {
    public:
        shared_scoped_context(std::shared_ptr<ConcreteContext> ctx):
            ctx_ { std::make_shared<unique_scoped_context<ConcreteContext>>(std::move(ctx)) }
        {}

        auto get() const noexcept {
            return ctx_->get();
        }

        auto operator->() const noexcept {
            return get();
        }

        auto &operator*() const noexcept {
            return *get();
        }

    private:
        std::shared_ptr<unique_scoped_context<ConcreteContext>> ctx_;
    };

    /**
     * Awaitable for context creation. Generally fulfils the same purpose as context_state_awaitable
     * but will return a scoped wrapper around the new context.
     */
    template<std::derived_from<context_base> ConcreteContext>
    class create_context_awaitable {
    public:
        create_context_awaitable(std::shared_ptr<ConcreteContext> ctx, context_state_awaitable start_awaitable):
            ctx_ { std::move(ctx) }, start_awaitable_ { std::move(start_awaitable) }
        {}

        auto await_ready() const noexcept { return start_awaitable_.await_ready(); }
        auto await_resume() const noexcept { return shared_scoped_context { ctx_ }; }
        auto await_suspend(std::coroutine_handle<> handle) noexcept {
            start_awaitable_.await_suspend(handle);
        }

    private:
        std::shared_ptr<ConcreteContext> ctx_;
        context_state_awaitable start_awaitable_;
    };

    /**
     * active-children registry for context parents.
     */
    template<std::derived_from<context_base> BaseContext = context_base>
    class dependent_contexts {
    public:
        dependent_contexts() {
            active_contexts_.max_load_factor(0.75);
        }

        /**
         * called when a child context has been stopped to remove it from the registry
         */
        auto remove_stopped_context(BaseContext *stopped_ctx) -> void {
            active_contexts_.erase(stopped_ctx);
        }

        /**
         * Create a new context, start it, put it in the registry, and return an awaitable with which
         * a scoped wrapper around the new context can be co_awaited.
         */
        template<
            std::derived_from<BaseContext> ConcreteContext,
            std::derived_from<context_parent> Parent,
            typename... Args
        > auto create_context(
            Parent *parent,
            Args&&... args
        ) {
            logger->trace("dependent_contexts::create_context, parent = {}", static_cast<void*>(parent));

            auto new_context = std::make_shared<ConcreteContext>(parent, std::forward<Args>(args)...);

            auto start_awaitable = new_context->start();
            active_contexts_[new_context.get()] = new_context;

            return create_context_awaitable { std::move(new_context), std::move(start_awaitable) };
        }

        /**
         * @return number of active children
         */
        auto size() const noexcept {
            return active_contexts_.size();
        }

        /**
         * @return true if there are no active children left
         */
        auto empty() const noexcept -> bool {
            return active_contexts_.empty();
        }

        /**
         * Request to stop all child contexts. After this we expect remove_stopped_context to be called
         * for all of them once they've been stopped.
         */
        auto stop_all() -> void {
            // We need this iterator dance because it->second->stop() may remove the context from active_children,
            // and the iterator pointing to it will be invalidated. This way, next remains valid in that case.
            auto next = active_contexts_.begin();

            while(next != active_contexts_.end()) {
                auto it = next;
                ++next;

                try {
                    static_cast<void>(it->second->stop());
                } catch(doca_exception &e) {
                    logger->error("unable to stop child context {}", static_cast<void*>(it->second.get()));
                }
            }
        }

    private:
        std::unordered_map<BaseContext*, std::shared_ptr<BaseContext>> active_contexts_;
    };
}
