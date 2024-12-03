#include "producer.hpp"

#include <doca/common/status.hpp>
#include <doca/logger.hpp>

#include <cassert>

namespace doca::comch {
    producer::producer(
        context_parent *parent,
        doca_comch_connection *connection,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        doca_comch_producer *raw_handle = nullptr;
        enforce_success(doca_comch_producer_create(connection, &raw_handle));
        handle_.reset(raw_handle);

        context::init_state_changed_callback();

        enforce_success(doca_comch_producer_task_send_set_conf(
            handle_.handle(),
            &plain_status_callback<&doca_comch_producer_task_send_as_task>,
            &plain_status_callback<&doca_comch_producer_task_send_as_task>,
            max_tasks
        ));
    }

    producer::~producer() {
        assert(doca_state() == DOCA_CTX_STATE_IDLE);
    }

    auto producer::send(
        buffer buf,
        std::span<std::uint8_t> immediate_data,
        std::uint32_t consumer_id
    ) -> coro::status_awaitable<> {
        doca_comch_producer_task_send *task = nullptr;

        auto result = coro::status_awaitable<>::create_space();
        auto receptable = result.receptable_ptr();

        doca_data task_user_data = { .ptr = receptable };

        enforce_success(doca_comch_producer_task_send_alloc_init(
            handle_.handle(),
            buf.handle(),
            immediate_data.data(),
            immediate_data.size(),
            consumer_id,
            &task
        ));

        auto base_task = doca_comch_producer_task_send_as_task(task);
        doca_task_set_user_data(base_task, task_user_data);

        engine()->submit_task(base_task, receptable);

        return result;
    }
}
