#pragma once

#include "error.hpp"

#include <doca_ctx.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <vector>

namespace doca {
    class progress_engine;

    class context {
    public:
        virtual ~context() = default;

        context(context const &) = delete;
        context &operator=(context const &) = delete;

        virtual auto stop() -> void;

        [[nodiscard]]
        virtual auto as_ctx() const -> doca_ctx* = 0;

        [[nodiscard]]
        auto get_state() const -> doca_ctx_states;

    protected:
        context() = default;
        auto init_state_changed_callback() -> void;
        auto signal_context_stopped() -> void;

        static auto state_changed_entry(
            doca_data user_data,
            doca_ctx *ctx,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void;

        virtual auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void;

    private:
        template<std::derived_from<context> BaseContext>
        friend class dependent_contexts;

        auto connect_to(progress_engine *engine) -> void;

        progress_engine *engine_ = nullptr;
    };

    template<std::derived_from<context> BaseContext>
    class dependent_contexts {
    public:
        auto remove_stopped_context(BaseContext *stopped_ctx) {
            auto it = std::ranges::find_if(
                active_contexts_,
                [stopped_ctx](auto &p) {
                    return p.get() == stopped_ctx;
                }
            );

            if(it != active_contexts_.end()) {
                active_contexts_.erase(it);
            }            
        }

        template<std::derived_from<BaseContext> ConcreteContext, typename... Args>
        auto create_context(progress_engine *engine, Args&&... args) -> ConcreteContext * {
            // make sure push_back will not throw an exception
            if(active_contexts_.capacity() == active_contexts_.size()) {
                auto double_capacity = std::max(std::size_t(8), active_contexts_.capacity() * 2);
                active_contexts_.reserve(double_capacity);
            }

            auto new_context = std::make_unique<ConcreteContext>(std::forward<Args>(args)...);
            new_context->connect_to(engine);
            
            auto err = doca_ctx_start(new_context->as_ctx());

            if(
                err != DOCA_SUCCESS && 
                err != DOCA_ERROR_IN_PROGRESS
            ) {
                throw doca_exception(err);
            }

            auto non_owning_ptr = new_context.get();
            active_contexts_.push_back(std::move(new_context));

            return non_owning_ptr;
        }

        auto &active_contexts() {
            return active_contexts_;
        }

    private:
        std::vector<std::unique_ptr<BaseContext>> active_contexts_;
    };

    template<typename ChildContext>
    class context_parent {
    public:
        context_parent() {
            static_assert(std::derived_from<ChildContext, context>);
        }

        virtual ~context_parent() = default;
        virtual auto signal_stopped_child(ChildContext *stopped_child) -> void = 0;
    };
}
