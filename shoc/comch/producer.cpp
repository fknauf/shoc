#include "producer.hpp"

#include <shoc/common/status.hpp>
#include <shoc/logger.hpp>

#include <cassert>

namespace shoc::comch {
    producer::producer(
        context_parent *parent,
        doca_comch_connection *connection,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_comch_producer_create>(connection)
        }
    {
        enforce_success(doca_comch_producer_task_send_set_conf(
            handle(),
            &plain_status_callback<&doca_comch_producer_task_send_as_task>,
            &plain_status_callback<&doca_comch_producer_task_send_as_task>,
            max_tasks
        ));
    }

    auto producer::send(
        buffer buf,
        std::span<std::uint8_t> immediate_data,
        shared_remote_consumer const &destination
    ) -> coro::status_awaitable<> {
        if(destination->expired()) {
            return coro::status_awaitable<>::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_producer_task_send *task = nullptr;

        auto result = coro::status_awaitable<>::create_space();
        auto receptable = result.receptable_ptr();

        doca_data task_user_data = { .ptr = receptable };

        enforce_success(doca_comch_producer_task_send_alloc_init(
            handle(),
            buf.handle(),
            immediate_data.data(),
            immediate_data.size(),
            destination->id(),
            &task
        ));

        auto base_task = doca_comch_producer_task_send_as_task(task);
        doca_task_set_user_data(base_task, task_user_data);

        engine()->submit_task(base_task, receptable);

        return result;
    }
}
