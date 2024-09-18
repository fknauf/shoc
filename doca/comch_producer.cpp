#include "comch_producer.hpp"

#include "logger.hpp"

#include <cassert>

namespace doca {
    base_comch_producer::base_comch_producer(
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
            &base_comch_producer::send_completion_entry,
            &base_comch_producer::send_error_entry,
            max_tasks
        ));
    }

    base_comch_producer::~base_comch_producer() {
        assert(get_state() == DOCA_CTX_STATE_IDLE);
    }

    auto base_comch_producer::send(
        buffer buf,
        std::span<std::uint8_t> immediate_data,
        std::uint32_t consumer_id,
        doca_data task_user_data
    ) -> void {
        doca_comch_producer_task_send *task = nullptr;

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

        if(
            auto err = doca_task_submit(base_task); 
            err != DOCA_SUCCESS
        ) {
            doca_task_free(base_task);
            throw doca_exception(err);
        }
    }

    auto base_comch_producer::send_completion_entry(
        [[maybe_unused]] doca_comch_producer_task_send *task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto producer = static_cast<base_comch_producer*>(base_context);

        if(producer == nullptr) {
            logger->error("got send completion event without comch_producer");
        } else {
            try {
                producer->send_completion(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_producer send completion event handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_producer send completion event handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_producer_task_send_as_task(task));
    }
        
    auto base_comch_producer::send_error_entry(
        doca_comch_producer_task_send *task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto producer = static_cast<base_comch_producer*>(base_context);

        if(producer == nullptr) {
            logger->error("got send error event without comch_producer");
        } else {
            try {
                producer->send_error(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_producer send error event handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_producer send error event handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_producer_task_send_as_task(task));
    }

    auto base_comch_producer::stop() -> void {
        enforce_success(doca_ctx_stop(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
    }

    comch_producer::comch_producer(
        context_parent *parent,
        doca_comch_connection *connection,
        std::uint32_t max_tasks,
        comch_producer_callbacks callbacks
    ):
        base_comch_producer { parent, connection, max_tasks },
        callbacks_ { std::move(callbacks) }
    {}

    auto comch_producer::state_changed(
        doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        if(callbacks_.state_changed) {
            callbacks_.state_changed(*this, prev_state, next_state);
        }

        base_comch_producer::state_changed(prev_state, next_state);
    }

    auto comch_producer::send_completion(
        doca_comch_producer_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_completion) {
            callbacks_.send_completion(*this, task, task_user_data);
        }
    }

    auto comch_producer::send_error(
        doca_comch_producer_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_completion) {
            callbacks_.send_error(*this, task, task_user_data);
        }
    }
}
