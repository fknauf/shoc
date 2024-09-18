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

        auto engine() { 
            return engine_;
        }

    private:
        template<std::derived_from<context> BaseContext>
        friend class dependent_contexts;

        auto connect_to(progress_engine *engine) -> void;

        progress_engine *engine_ = nullptr;
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
            progress_engine *engine,
            Args&&... args
        ) -> ConcreteContext * {
            auto new_context = std::make_unique<ConcreteContext>(std::forward<Args>(args)...);
            auto non_owning_ptr = new_context.get();

            // make sure slot exists so inserting will not throw later.
            auto &slot = active_contexts_[non_owning_ptr];

            new_context->connect_to(engine);
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

    class context_parent {
    public:
        virtual ~context_parent() = default;
        virtual auto signal_stopped_child(context *stopped_child) -> void = 0;
    };
}
