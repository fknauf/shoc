#include "context.hpp"
#include "error.hpp"
#include "progress_engine.hpp"

#include <cassert>

namespace doca {
    context::context(context_parent *parent):
        parent_ { parent }
    {
        assert(parent != nullptr);
    }

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

        assert(obj != nullptr);

        obj->state_changed(prev_state, next_state);

        if(
            next_state == DOCA_CTX_STATE_IDLE &&
            obj->parent_ != nullptr
        ) {
            obj->parent_->signal_stopped_child(obj);
        }
    }

    auto context::get_state() const -> doca_ctx_states {
        doca_ctx_states state;
        enforce_success(doca_ctx_get_state(as_ctx(), &state));
        return state;
    }

    auto context::stop() -> void {
        enforce_success(doca_ctx_stop(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
    }

    auto context::connect() -> void {
        engine()->connect(this);
    }

    auto context::inflight_tasks() const -> std::size_t {
        std::size_t tasks;
        enforce_success(doca_ctx_get_num_inflight_tasks(as_ctx(), &tasks));
        return tasks;
    }
}
