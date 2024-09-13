#include "context.hpp"
#include "error.hpp"
#include "progress_engine.hpp"

namespace doca {
    auto context::init_state_changed_callback() -> void {
        auto ctx = as_ctx();

        doca_data ctx_user_data = { .ptr = this };
        enforce_success(doca_ctx_set_user_data(ctx, ctx_user_data));
        enforce_success(doca_ctx_set_state_changed_cb(ctx, &context::state_changed_entry));
    }

    auto context::state_changed_entry(
        doca_data user_data,
        [[maybe_unused]] doca_ctx *ctx,
        doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        auto obj = static_cast<context*>(user_data.ptr);
        obj->state_changed(prev_state, next_state);
    }

    auto context::state_changed(
        [[maybe_unused]] doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        if(
            next_state == DOCA_CTX_STATE_IDLE &&
            engine_ != nullptr
        ) {
            signal_context_stopped();
        }
    }    

    auto context::signal_context_stopped() -> void {
        engine_->remove_stopped_context(this);
    }

    auto context::get_state() const -> doca_ctx_states {
        doca_ctx_states state;
        enforce_success(doca_ctx_get_state(as_ctx(), &state));
        return state;
    }

    auto context::stop() -> void {
        doca_ctx_stop(as_ctx());
    }

    auto context::connect_to(
        progress_engine *engine
    ) -> void {
        engine_ = engine;
        engine->connect(this);
    }
}
