#include "context.hpp"
#include "error.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"

#include <cassert>

namespace shoc {
    context_base::context_base(context_parent *parent):
        parent_ { parent }
    {
        assert(parent != nullptr);

        logger->trace("context::context parent = {}", static_cast<void*>(parent));
    }

    auto context_base::start() -> context_state_awaitable {
        logger->trace("requesting context start, this = {}, as_ctx() = {}", static_cast<void*>(this), static_cast<void*>(as_ctx()));
        enforce_success(doca_ctx_start(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
        logger->trace("context start requested");

        return { shared_from_this(), DOCA_CTX_STATE_RUNNING };
    }

    auto context_base::stop() -> context_state_awaitable {
        auto ctx = as_ctx();

        if(ctx != nullptr) {
            enforce_success(doca_ctx_stop(ctx), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
        } else {
            logger->warn("trying to stop a context that's already stopped");
        }

        return { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto context_base::state() const -> context_state {
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

    auto context_base::inflight_tasks() const -> std::size_t {
        std::size_t tasks;
        enforce_success(doca_ctx_get_num_inflight_tasks(as_ctx(), &tasks));
        return tasks;
    }

    auto context_base::connect_to_engine() -> void {
        engine()->connect(this);        
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
