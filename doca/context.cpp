#include "context.hpp"
#include "error.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"

#include <cassert>

namespace doca {
    context::context(context_parent *parent):
        parent_ { parent }
    {
        assert(parent != nullptr);

        logger->trace("context::context parent = {}", static_cast<void*>(parent));
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

        obj->current_state_ = next_state;
        obj->state_changed(prev_state, next_state);

        if(next_state == DOCA_CTX_STATE_RUNNING) {
            logger->debug("context started");

            if(obj->coro_start_) {
                logger->debug("continuing start coroutine");
                obj->coro_start_.resume();
            }
        } else if(next_state == DOCA_CTX_STATE_IDLE ) {
            logger->debug("context stopped");

            if(obj->coro_stop_) {
                logger->debug("continuing stop coroutine");
                obj->coro_stop_.resume();
            }

            obj->parent_->signal_stopped_child(obj);
        }
    }

    auto context::start() -> context_state_awaitable {
        logger->trace("requesting context start, this = {}, as_ctx() = {}", static_cast<void*>(this), static_cast<void*>(as_ctx()));
        enforce_success(doca_ctx_start(as_ctx()));
        logger->trace("context start requested");

        return { this, DOCA_CTX_STATE_RUNNING };
    }

    auto context::stop() -> context_state_awaitable {
        enforce_success(doca_ctx_stop(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });

        return { this, DOCA_CTX_STATE_IDLE };
    }

    auto context::connect() -> void {
        engine()->connect(this);
    }

    auto context::inflight_tasks() const -> std::size_t {
        std::size_t tasks;
        enforce_success(doca_ctx_get_num_inflight_tasks(as_ctx(), &tasks));
        return tasks;
    }

    auto context_state_awaitable::await_ready() const noexcept -> bool {
        // depending on the concrete context, doca_ctx_start will go either into DOCA_CTX_STATE_STARTING,
        // in which case this awaitable has to suspend and wait for an async event to continue, or
        // directly into DOCA_CTX_STATE_RUNNING, in which case suspension is unnecessary.
        return ctx_->get_state() == desired_state_;
    }

    auto context_state_awaitable::await_suspend(std::coroutine_handle<> caller) noexcept -> void {
        switch(desired_state_) {
            case DOCA_CTX_STATE_RUNNING:
                ctx_->coro_start_ = caller;
                break;
            case DOCA_CTX_STATE_IDLE:
                ctx_->coro_stop_ = caller;
                break;
            default:
                break;
        }
    }
}
