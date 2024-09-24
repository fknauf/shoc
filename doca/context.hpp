#pragma once

#include "coroutine.hpp"
#include "error.hpp"

#include <doca_ctx.h>

#include <algorithm>
#include <concepts>
#include <coroutine>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace doca {
    class context;
    class progress_engine;

    class context_parent {
    public:
        virtual ~context_parent() = default;

        virtual auto signal_stopped_child(context *stopped_child) -> void = 0;
        virtual auto engine() -> progress_engine* = 0;
    };

    class [[nodiscard]] context_state_awaitable {
    public:
        context_state_awaitable(context *ctx, doca_ctx_states desired_state):
            ctx_(ctx), desired_state_(desired_state)
        {
            logger->trace("context_state_awaitable ctx = {}, desired_state = {}", static_cast<void*>(ctx_), desired_state_);
        }

        auto await_ready() const noexcept -> bool;
        auto await_resume() const noexcept {}

        auto await_suspend(std::coroutine_handle<> caller) noexcept -> void;

    private:
        context *ctx_;
        doca_ctx_states desired_state_;
    };

    class context:
        public context_parent
    {
    public:
        friend class context_state_awaitable;

        context(context_parent *parent);

        virtual ~context() = default;

        context(context const &) = delete;
        context &operator=(context const &) = delete;

        [[nodiscard]]
        auto start() -> context_state_awaitable;

        [[nodiscard]]
        virtual auto stop() -> context_state_awaitable;

        [[nodiscard]]
        virtual auto as_ctx() const -> doca_ctx* = 0;

        [[nodiscard]]
        auto get_state() const -> doca_ctx_states {
            return current_state_;
        }

        auto signal_stopped_child([[maybe_unused]] context *stopped_child) -> void override {}

        [[nodiscard]]
        auto engine() -> progress_engine* override {
            return parent_->engine();
        }

        [[nodiscard]]
        auto inflight_tasks() const -> std::size_t;

    protected:
        auto init_state_changed_callback() -> void;

        virtual auto state_changed(
            [[maybe_unused]] doca_ctx_states prev_state,
            [[maybe_unused]] doca_ctx_states next_state
        ) -> void {
        }

    private:
        template<std::derived_from<context> BaseContext>
        friend class dependent_contexts;

        static auto state_changed_entry(
            doca_data user_data,
            doca_ctx *ctx,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void;

        auto connect() -> void;

        context_parent *parent_ = nullptr;
        doca_ctx_states current_state_ = DOCA_CTX_STATE_IDLE;

        std::coroutine_handle<> coro_start_;
        std::coroutine_handle<> coro_stop_;
    };

    template<std::derived_from<context> BaseContext = context>
    class dependent_contexts {
    public:
        dependent_contexts() {
            active_contexts_.max_load_factor(0.75);
        }

        auto remove_stopped_context(BaseContext *stopped_ctx) -> void {
            active_contexts_.erase(stopped_ctx);
        }

        template<
            std::derived_from<BaseContext> ConcreteContext,
            std::derived_from<context_parent> Parent,
            typename... Args
        > auto create_context(
            Parent *parent,
            Args&&... args
        ) -> coro::task<ConcreteContext*> {
            logger->trace("dependent_contexts::create_context, parent = {}", static_cast<void*>(parent));

            auto new_context = std::make_unique<ConcreteContext>(parent, std::forward<Args>(args)...);
            new_context->connect();

            return create_context_coro(std::move(new_context));
        }

        template<std::derived_from<BaseContext> ConcreteContext>
        auto create_context_coro(std::unique_ptr<ConcreteContext> new_context) -> coro::task<ConcreteContext*> {
            auto non_owning_ptr = new_context.get();
            auto &slot = active_contexts_[non_owning_ptr];

            co_await new_context->start();
            slot = std::move(new_context);

            co_return non_owning_ptr;
        }

        auto size() const noexcept {
            return active_contexts_.size();
        }

        auto empty() const noexcept -> bool {
            return active_contexts_.empty();
        }

        auto stop_all() -> void {
            for(auto &child : active_contexts_) {
                doca_ctx_stop(child.second->as_ctx());
            }
        }

    private:
        //std::vector<std::unique_ptr<BaseContext>> active_contexts_;
        std::unordered_map<BaseContext*, std::unique_ptr<BaseContext>> active_contexts_;
    };
}
