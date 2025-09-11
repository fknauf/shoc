#include "producer.hpp"

#include <shoc/logger.hpp>
#include <shoc/progress_engine.hpp>

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
            shoc::logger->debug("producer cannot send, remote consumer is expired");
            return coro::status_awaitable<>::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        return detail::plain_status_offload<
            doca_comch_producer_task_send_alloc_init,
            doca_comch_producer_task_send_as_task
        >(
            engine(),
            handle(),
            buf.handle(),
            immediate_data.data(),
            immediate_data.size(),
            destination->id()
        );
    }
}
