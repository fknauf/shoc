#pragma once

#include "error.hpp"

#include <doca_ctx.h>

#include <algorithm>
#include <concepts>
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

    class context:
        public context_parent
    {
    public:
        context(context_parent *parent);

        virtual ~context() = default;

        context(context const &) = delete;
        context &operator=(context const &) = delete;

        virtual auto stop() -> void;

        [[nodiscard]]
        virtual auto as_ctx() const -> doca_ctx* = 0;

        [[nodiscard]]
        auto get_state() const -> doca_ctx_states;

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
            typename... Args
        > auto create_context(
            Args&&... args
        ) -> ConcreteContext * {
            auto new_context = std::make_unique<ConcreteContext>(std::forward<Args>(args)...);
            auto non_owning_ptr = new_context.get();

            // make sure slot exists so inserting will not throw later.
            auto &slot = active_contexts_[non_owning_ptr];

            new_context->connect();
            enforce_success(doca_ctx_start(new_context->as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });

            slot = std::move(new_context);

            return non_owning_ptr;
        }

        auto size() const noexcept {
            return active_contexts_.size();
        }

        auto empty() const noexcept -> bool {
            return active_contexts_.empty();
        }

        auto stop_all() -> void {
            for(auto &child : active_contexts_) {
                child.second->stop();
            }
        }

    private:
        //std::vector<std::unique_ptr<BaseContext>> active_contexts_;
        std::unordered_map<BaseContext*, std::unique_ptr<BaseContext>> active_contexts_;
    };
}
