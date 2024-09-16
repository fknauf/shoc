#include "comch_client.hpp"
#include "logger.hpp"

#include <doca_pe.h>

namespace doca {
    namespace {
        auto get_comch_client_from_connection(doca_comch_connection *comch_connection) -> base_comch_client* {
            auto client_handle = doca_comch_client_get_client_ctx(comch_connection);
            auto client_ctx = doca_comch_client_as_ctx(client_handle);

            doca_data user_data;
            auto err = doca_ctx_get_user_data(client_ctx, &user_data);

            if(err != DOCA_SUCCESS) {
                auto errmsg = doca_error_get_name(err);
                logger->error("comch_server new connection: could not get user data from ctx, errmsg = {}", errmsg);
                return nullptr;
            }

            auto base_context = static_cast<context*>(user_data.ptr);
            return static_cast<base_comch_client*>(base_context);
        }
    }

    base_comch_client::base_comch_client(
        std::string const &server_name,
        comch_device &dev,
        comch_client_limits const &limits
    ) {
        doca_comch_client *doca_client;

        enforce_success(doca_comch_client_create(dev.handle(), server_name.c_str(), &doca_client));
        handle_.reset(doca_client);

        context::init_state_changed_callback();

        enforce_success(doca_comch_client_task_send_set_conf(
            handle(),
            &base_comch_client::send_completion_entry,
            &base_comch_client::send_error_entry,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_client_event_msg_recv_register(
            handle(),
            &comch_client::msg_recv_entry
        ));
        enforce_success(doca_comch_client_event_consumer_register(
            handle(),
            &comch_client::new_consumer_entry,
            &comch_client::expired_consumer_entry
        ));
        enforce_success(doca_comch_client_set_max_msg_size(handle(), limits.max_msg_size));
        enforce_success(doca_comch_client_set_recv_queue_size(handle(), limits.recv_queue_size));
    }

    base_comch_client::~base_comch_client() {
        doca_ctx_stop(as_ctx());
    }

    auto base_comch_client::as_ctx() const -> doca_ctx* {
        return doca_comch_client_as_ctx(handle());
    }

    auto base_comch_client::submit_message(std::string message, doca_data task_user_data) -> void {
        doca_comch_connection *connection;
        doca_comch_task_send *task;

        enforce_success(doca_comch_client_get_connection(handle_.handle(), &connection));
        enforce_success(doca_comch_client_task_send_alloc_init(handle_.handle(), connection, message.data(), message.size(), &task));
        doca_task_set_user_data(doca_comch_task_send_as_task(task), task_user_data);

        auto base_task = doca_comch_task_send_as_task(task);
        auto err = doca_task_submit(base_task);

        if(err != DOCA_SUCCESS) {
            doca_task_free(base_task);
            throw doca_exception(err);
        }
    }

    auto base_comch_client::send_completion_entry(
        [[maybe_unused]] doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto client = static_cast<base_comch_client*>(base_context);

        if(client == nullptr) {
            logger->error("got send completion event without comch_client");
        } else {
            try {
                client->send_completion(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_client send completion handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_client send completion handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_task_send_as_task(task));
    }

    auto base_comch_client::send_error_entry(
        [[maybe_unused]] doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto client = static_cast<base_comch_client*>(base_context);

        if(client == nullptr) {
            logger->error("got send error event without comch_client");
        } else {
            try {
                client->send_error(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_client send error handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_client send error handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_task_send_as_task(task));
    }
    
    auto base_comch_client::msg_recv_entry(
        [[maybe_unused]] doca_comch_event_msg_recv *event,
        std::uint8_t *recv_buffer,
        std::uint32_t msg_len,
        doca_comch_connection *comch_connection
    ) -> void {
        auto client = get_comch_client_from_connection(comch_connection);

        if(client == nullptr) {
            logger->error("got msg recv event without comch_client instance");
            return;
        }

        try {
            auto msg = std::span { recv_buffer, static_cast<std::size_t>(msg_len) };
            client->msg_recv(msg, comch_connection);
        } catch(std::exception &e) {
            logger->error("comch_client msg recv handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_client msg recv handler failed with unknown error");
        }
    }
    
    auto base_comch_client::new_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t id
    ) -> void {
        logger->debug("comch_client new consumer, id = {}", id);

        auto client = get_comch_client_from_connection(comch_connection);

        if(client == nullptr) {
            logger->error("got new consumer event without comch_client instance");
            return;
        }

        try {
            client->new_consumer(comch_connection, id);
        } catch(std::exception &e) {
            logger->error("comch_client new consumer handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_client new consumer handler failed with unknown error");
        }
    }
    
    auto base_comch_client::expired_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t id
    ) -> void 
    {
        logger->debug("comch_client new consumer, id = {}", id);

        auto client = get_comch_client_from_connection(comch_connection);

        if(client == nullptr) {
            logger->error("got expired consumer event without comch_client instance");
            return;
        }

        try {
            client->expired_consumer(comch_connection, id);
        } catch(std::exception &e) {
            logger->error("comch_client expired consumer handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_client expired consumer handler failed with unknown error");
        }
    }

    auto base_comch_client::stop() -> void {
        stop_requested_ = true;

        for(auto &child : active_consumers_.active_contexts()) {
            child->stop();
        }

        do_stop_if_able();
    }

    auto base_comch_client::signal_stopped_child(comch_consumer *stopped_child) -> void {
        active_consumers_.remove_stopped_context(stopped_child);
        if(stop_requested_) {
            do_stop_if_able();
        }
    }

    auto base_comch_client::do_stop_if_able() -> void {
        if(active_consumers_.active_contexts().empty()) {
            context::stop();
        }
    }

    auto base_comch_client::create_consumer(
        memory_map &mmap,
        std::uint32_t max_tasks,
        comch_consumer_callbacks callbacks
    ) -> comch_consumer* {
        return active_consumers_.create_context<comch_consumer>(engine(), this, this->get_connection(), mmap, max_tasks, std::move(callbacks));
    }

    comch_client::comch_client(
        std::string const &server_name,
        comch_device &dev,
        comch_client_callbacks callbacks,
        comch_client_limits const &limits
    ):
        base_comch_client { server_name, dev, limits },
        callbacks_ { std::move(callbacks) }
    { }

    auto comch_client::state_changed(
        doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        logger->debug("comch_client: state change {} -> {}", prev_state, next_state);

        if(callbacks_.state_changed) {
            callbacks_.state_changed(*this, prev_state, next_state);
        }

        base_comch_client::state_changed(prev_state, next_state);
    }

    auto comch_client::send_completion(
        doca_comch_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_completion) {
            callbacks_.send_completion(*this, task, task_user_data);
        }
    }
        
    auto comch_client::send_error(
        doca_comch_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_error) {
            callbacks_.send_error(*this, task, task_user_data);
        }
    }
        
    auto comch_client::msg_recv(
        std::span<std::uint8_t> recv_buffer,
        doca_comch_connection *comch_connection
    ) -> void {
        if(callbacks_.message_received) {
            callbacks_.message_received(*this, recv_buffer, comch_connection);
        }
    }
}
