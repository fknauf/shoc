#include "comch_consumer.hpp"

#include <doca/logger.hpp>

namespace doca {
    class task_post_recv {
    public:
        task_post_recv(doca_comch_consumer_task_post_recv *task):
            handle_(task)
        { }

        ~task_post_recv() {
            doca_task_free(doca_comch_consumer_task_post_recv_as_task(handle_));
        }

        auto get_buf() -> buffer {
            return { doca_comch_consumer_task_post_recv_get_buf(handle_) };
        }

        auto get_status() const {
            return doca_task_get_status(as_task());
        }

    private:
        auto as_task() const noexcept -> doca_task* {
            return doca_comch_consumer_task_post_recv_as_task(handle_);
        }

        doca_comch_consumer_task_post_recv *handle_;
    };

    comch_consumer::comch_consumer(
        context_parent<comch_consumer> *parent,
        doca_comch_connection *connection,
        memory_map &user_mmap,
        std::uint32_t max_tasks,
        comch_consumer_callbacks callbacks
    ):
        parent_ { parent },
        callbacks_ { std::move(callbacks) }
    {
        doca_comch_consumer* raw_consumer = nullptr;

        enforce_success(doca_comch_consumer_create(connection, user_mmap.handle(), &raw_consumer));
        handle_.reset(raw_consumer);

        context::init_state_changed_callback();

        enforce_success(doca_comch_consumer_task_post_recv_set_conf(
            handle_.handle(),
            &comch_consumer::post_recv_task_completion_entry,
            &comch_consumer::post_recv_task_error_entry,
            max_tasks
        ));
    }

    auto comch_consumer::post_recv_msg(buffer dest, doca_data task_user_data) -> void {
        doca_comch_consumer_task_post_recv *task;

        enforce_success(doca_comch_consumer_task_post_recv_alloc_init(handle_.handle(), dest.handle(), &task));
        auto base_task = doca_comch_consumer_task_post_recv_as_task(task);

        doca_task_set_user_data(base_task, task_user_data);

        if(
            auto err = doca_task_submit(base_task); 
            err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS
        ) {
            doca_task_free(base_task);
            throw doca_exception(err);
        }

        doca_buf_inc_refcount(dest.handle(), nullptr);
    }

    auto comch_consumer::post_recv_task_completion_entry(
        doca_comch_consumer_task_post_recv *raw_task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto consumer = static_cast<comch_consumer*>(base_context);

        auto task = comch_consumer_task_post_recv { raw_task, task_user_data };

        if(consumer == nullptr) {
            logger->error("got post_recv completion event without comch_consumer");
        } else {
            try {
                callbacks_.post_recv_completion(*this, task);
            } catch(std::exception &ex) {
                logger->error("comch_consumer: post_recv completion event handler failed: {}", ex.what());
            } catch(...) {
                logger->error("comch_consumer: post_recv completion event handler failed with unknown error");
            }
        }
    }
 
    auto comch_consumer::post_recv_task_error_entry(
        doca_comch_consumer_task_post_recv *task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto consumer = static_cast<comch_consumer*>(base_context);

        auto task = comch_consumer_task_post_recv { raw_task, task_user_data };

        if(consumer == nullptr) {
            logger->error("got post_recv error event without comch_consumer");
        } else {
            try {
                callbacks_.post_recv_error(*this, task);
            } catch(std::exception &ex) {
                logger->error("comch_consumer: post_recv error event handler failed: {}", ex.what());
            } catch(...) {
                logger->error("comch_consumer: post_recv error event handler failed with unknown error");
            }
        }
    }

    auto comch_consumer::state_changed(
        doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        if(callbacks_.state_changed) {
            callbacks_.state_changed(*this, prev_state, next_state);
        }

        if(
            next_state == DOCA_CTX_STATE_IDLE &&
            parent_ != nullptr
        ) {
            parent_->signal_stopped_child(this);
        }
    }

    auto comch_consumer::stop() -> void {
        enforce_success(doca_ctx_stop(handle_.handle()));
        parent_->signal_stopped_child(this);
    }
}
