#pragma once

#include "error.hpp"

#include <doca_ctx.h>

#include <functional>

namespace doca {
    class progress_engine;

    class context {
    public:
        virtual ~context() = default;

        context(context const &) = delete;
        context &operator=(context const &) = delete;

        auto stop() -> void;

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
        friend class progress_engine;

        auto set_progress_engine(progress_engine *engine) -> void {
            engine_ = engine;
        }

        progress_engine *engine_ = nullptr;
    };
}
