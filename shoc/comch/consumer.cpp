#include "consumer.hpp"

#include <shoc/logger.hpp>
#include <shoc/progress_engine.hpp>

#include <cassert>

namespace shoc::comch {
    consumer::consumer(
        context_parent *parent,
        doca_comch_connection *connection,
        memory_map &user_mmap,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_comch_consumer_create>(connection, user_mmap.handle())
        }
    {
        enforce_success(doca_comch_consumer_task_post_recv_set_conf(
            handle(),
            &consumer::post_recv_task_completion_callback,
            &consumer::post_recv_task_completion_callback,
            max_tasks
        ));
    }

    auto consumer::post_recv(buffer &dest) -> consumer_recv_awaitable {
        doca_comch_consumer_task_post_recv *task;

        auto result = consumer_recv_awaitable::create_space();
        auto receptable = result.receptable_ptr();

        doca_data task_user_data = { .ptr = receptable };

        enforce_success(doca_comch_consumer_task_post_recv_alloc_init(handle(), dest.handle(), &task));
        auto base_task = doca_comch_consumer_task_post_recv_as_task(task);
        doca_task_set_user_data(base_task, task_user_data);

        engine()->submit_task(base_task, receptable);

        return result;
    }

    auto consumer::post_recv_task_completion_callback(
        doca_comch_consumer_task_post_recv *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto dest = static_cast<consumer_recv_awaitable::payload_type*>(task_user_data.ptr);
        auto base_task = doca_comch_consumer_task_post_recv_as_task(task);

        auto imm_base = doca_comch_consumer_task_post_recv_get_imm_data(task);
        auto imm_size = doca_comch_consumer_task_post_recv_get_imm_data_len(task);
        auto imm_start = reinterpret_cast<std::byte const*>(imm_base);
        auto imm_end = imm_start + imm_size;

        auto result = consumer_recv_result {
            .immediate = boost::container::static_vector<std::byte, MAX_IMMEDIATE_DATA_SIZE>(imm_start, imm_end),
            .producer_id = doca_comch_consumer_task_post_recv_get_producer_id(task),
            .status = doca_task_get_status(base_task)
        };

        doca_task_free(base_task);

        dest->set_value(std::move(result));
        dest->resume();
    }
}
