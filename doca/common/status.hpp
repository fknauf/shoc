#pragma once

#include <doca/coro/status_awaitable.hpp>

#include <cassert>
#include <cstdint>

#include <doca_error.h>
#include <doca_pe.h>

namespace doca {
    template<typename TaskType, doca_task* (*AsTask)(TaskType *)>
    auto plain_status_callback_function(
        TaskType *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) {
        assert(task_user_data.ptr != nullptr);

        auto dest = static_cast<typename coro::status_awaitable<>::payload_type*>(task_user_data.ptr);
        auto base_task = AsTask(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        dest->emplace_value(status);
        dest->resume();
    }
}
