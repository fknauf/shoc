#include "producer.hpp"

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
            &producer::send_completion_callback,
            &producer::send_completion_callback,
            max_tasks
        ));
    }

    producer::~producer() {
        assert(get_state() == DOCA_CTX_STATE_IDLE);
    }

    auto producer::send(
        buffer buf,
        std::span<std::uint8_t> immediate_data,
        std::uint32_t consumer_id
    ) -> status_awaitable {
        doca_comch_producer_task_send *task = nullptr;

        auto result = status_awaitable::create_space();
        doca_data task_user_data = { .ptr = result.dest.get() };

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

        engine()->submit_task(base_task, result.dest.get());

        return result;
    }

    auto producer::send_completion_callback(
        doca_comch_producer_task_send *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_task = doca_comch_producer_task_send_as_task(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        auto dest = static_cast<status_awaitable::payload_type*>(task_user_data.ptr);
        dest->value = status;
        dest->resume();
    }
}
