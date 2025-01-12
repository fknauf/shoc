#include "context.hpp"
#include "error.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"

#include <cassert>

namespace shoc {
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
        enforce_success(doca_ctx_set_state_changed_cb(ctx, &context::state_changed_callback));
    }

    auto context::state_changed_callback(
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
        } else if(next_state == DOCA_CTX_STATE_IDLE ) {
            logger->debug("context stopped");

            auto coro = std::exchange(obj->coro_stop_, nullptr);
            // may delete obj, so we can't use it after this.
            obj->parent_->signal_stopped_child(obj);

            if(coro) {
                coro.resume();
            }
        }
    }

    auto context::start() -> context_state_awaitable {
        logger->trace("requesting context start, this = {}, as_ctx() = {}", static_cast<void*>(this), static_cast<void*>(as_ctx()));
        enforce_success(doca_ctx_start(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
        logger->trace("context start requested");

        return { shared_from_this(), DOCA_CTX_STATE_RUNNING };
    }

    auto context::stop() -> context_state_awaitable {
        enforce_success(doca_ctx_stop(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });

        return { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto context::state() const -> context_state {
        // context should not be available to users while doca_ctx is starting.
        assert(doca_state() != DOCA_CTX_STATE_STARTING);

        switch(doca_state()) {
        case DOCA_CTX_STATE_IDLE:
            return context_state::idle;
        case DOCA_CTX_STATE_RUNNING:
            return preparing_stop() ? context_state::stopping : context_state::running;
        case DOCA_CTX_STATE_STOPPING:
            return context_state::stopping;
        case DOCA_CTX_STATE_STARTING:
        default:
            throw doca_error(DOCA_ERROR_BAD_STATE);
        }
    }

    auto context::connect_to_engine() -> void {
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
        return ctx_->doca_state() == desired_state_;
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
