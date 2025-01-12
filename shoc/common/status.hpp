#pragma once

#include <shoc/coro/status_awaitable.hpp>

#include <cassert>
#include <cstdint>

#include <doca_error.h>
#include <doca_pe.h>

namespace shoc {
    namespace detail {
        template<auto AsTask>
        struct deduce_as_task_arg_type {};

        template<typename TaskType, doca_task *AsTask(TaskType*)>
        struct deduce_as_task_arg_type<AsTask> {
            using type = TaskType;
        };

        template<auto AsTask>
        using deduce_as_task_arg_type_t = typename deduce_as_task_arg_type<AsTask>::type;
    }

    template<auto AsTask>
    auto plain_status_callback(
        detail::deduce_as_task_arg_type_t<AsTask> *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        assert(task_user_data.ptr != nullptr);

        auto dest = static_cast<typename coro::status_awaitable<>::payload_type*>(task_user_data.ptr);
        auto base_task = AsTask(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        dest->set_value(std::move(status));
        dest->resume();
    }
}
