#include "comch_server.hpp"
#include "logger.hpp"

#include <doca_pe.h>

#include <string_view>

namespace doca {
    namespace {
        auto get_comch_server_from_connection(doca_comch_connection *comch_connection) -> base_comch_server* {
            auto server_handle = doca_comch_server_get_server_ctx(comch_connection);
            auto server_ctx = doca_comch_server_as_ctx(server_handle);

            doca_data user_data;
            auto err = doca_ctx_get_user_data(server_ctx, &user_data);

            if(err != DOCA_SUCCESS) {
                auto errmsg = doca_error_get_name(err);
                logger->error("comch_server new connection: could not get user data from ctx, errmsg = {}", errmsg);
                return nullptr;
            }

            auto base_context = static_cast<context*>(user_data.ptr);
            return static_cast<base_comch_server*>(base_context);
        }
    }

    base_comch_server::base_comch_server(
        std::string const &server_name,
        comch_device &dev,
        device_representor &rep,
        comch_server_limits const &limits
    ) {
        doca_comch_server *doca_server;

        enforce_success(doca_comch_server_create(dev.handle(), rep.handle(), server_name.c_str(), &doca_server));
        handle_.reset(doca_server);

        context::init_state_changed_callback();

        enforce_success(doca_comch_server_task_send_set_conf(
            handle_.handle(),
            &base_comch_server::send_completion_entry,
            &base_comch_server::send_error_entry,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_server_event_msg_recv_register(
            handle_.handle(),
            &base_comch_server::msg_recv_entry
        ));
        enforce_success(doca_comch_server_event_connection_register(
            handle_.handle(),
            &base_comch_server::server_connection_entry,
            &base_comch_server::server_disconnection_entry
        ));
        enforce_success(doca_comch_server_event_consumer_register(
            handle_.handle(),
            &base_comch_server::new_consumer_entry,
            &base_comch_server::expired_consumer_entry
        ));
        enforce_success(doca_comch_server_set_max_msg_size(
            handle_.handle(),
            limits.max_msg_size
        ));
        enforce_success(doca_comch_server_set_recv_queue_size(
            handle_.handle(),
            limits.recv_queue_size
        ));
    }

    base_comch_server::~base_comch_server() {
        // server should already be stopped here because its owned by the progress_engine
        // that stops and waits for everything to finish before it's destroyed.
        stop();
    }

    auto base_comch_server::send_completion_entry(
        doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto server = static_cast<base_comch_server*>(base_context);

        if(server == nullptr) {
            logger->error("got send completion event without comch_server");
        } else {
            try {
                server->send_completion(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_server send completion handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_server send completion handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_task_send_as_task(task));
    }

    auto base_comch_server::send_error_entry(
        doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto server = static_cast<base_comch_server*>(base_context);

        if(server == nullptr) {
            logger->error("got send error event without comch_server");
        } else {
            try {
                server->send_error(task, task_user_data);
            } catch(std::exception &e) {
                logger->error("comch_server send error handler failed: {}", e.what());
            } catch(...) {
                logger->error("comch_server send error handler failed with unknown error");
            }
        }

        doca_task_free(doca_comch_task_send_as_task(task));
    }

    auto base_comch_server::msg_recv_entry(
        [[maybe_unused]] doca_comch_event_msg_recv *event,
        std::uint8_t *recv_buffer,
        std::uint32_t msg_len,
        doca_comch_connection *comch_connection
    ) -> void {
        auto server = get_comch_server_from_connection(comch_connection);

        if(server == nullptr) {
            logger->error("got msg recv event without comch_server");
            return;
        }

        try {
            server->msg_recv({ recv_buffer, msg_len }, comch_connection);
        } catch(std::exception &e) {
            logger->error("comch_server msg recv handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_server msg recv handler failed with unknown error");
        }
    }

    auto base_comch_server::server_connection_entry(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        doca_comch_connection *comch_connection,
        std::uint8_t change_successful
    ) -> void {
        auto server = get_comch_server_from_connection(comch_connection);

        if(server == nullptr) {
            logger->error("got server connection event without comch_server");
        }

        try {
            server->server_connection(comch_connection, change_successful);
        } catch(std::exception &e) {
            logger->error("comch_server server connection handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_server server connection handler failed with unknown error");
        }
    }

    auto base_comch_server::server_disconnection_entry(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        doca_comch_connection *comch_connection,
        std::uint8_t change_successful
    ) -> void {
        auto server = get_comch_server_from_connection(comch_connection);

        if(server == nullptr) {
            logger->error("got server disconnection event without comch_server");
            return;
        }

        try {
            server->server_disconnection(comch_connection, change_successful);
        } catch(std::exception &e) {
            logger->error("comch_server server disconnection handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_server server disconnection handler failed with unknown error");
        }
    }

    auto base_comch_server::new_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        auto server = get_comch_server_from_connection(comch_connection);

        if(server == nullptr) {
            logger->error("got new consumer event without comch_server");
            return;
        }

        try {
            server->new_consumer(comch_connection, remote_consumer_id);
        } catch(std::exception &e) {
            logger->error("comch_server new consumer handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_server new consumer handler failed with unknown error");
        }
    }

    auto base_comch_server::expired_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        auto server = get_comch_server_from_connection(comch_connection);

        if(server == nullptr) {
            logger->error("got expired consumer event without comch_server");
            return;
        }

        try {
            server->expired_consumer(comch_connection, remote_consumer_id);
        } catch(std::exception &e) {
            logger->error("comch_server expired consumer handler failed: {}", e.what());
        } catch(...) {
            logger->error("comch_server expired consumer handler failed with unknown error");
        }
    }

    auto base_comch_server::send_response(doca_comch_connection *con, std::string_view message) -> void {
        doca_comch_task_send *send_task;

        enforce_success(doca_comch_server_task_send_alloc_init(
            handle_.handle(),
            con,
            message.data(),
            message.size(),
            &send_task
        ));

        auto task = doca_comch_task_send_as_task(send_task);
        auto err = doca_task_submit(task);

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            throw doca_exception(err);
        }
    }

    auto base_comch_server::stop() -> void {
        // when active child contexts exist, stopping the server has to be delayed until they are all stopped.
        stop_requested_ = true;

        for(auto &child : active_producers_.active_contexts()) {
            child->stop();
        }

        do_stop_if_able();
    }

    auto base_comch_server::signal_stopped_child(base_comch_producer *stopped_child) -> void {
        active_producers_.remove_stopped_context(stopped_child);

        if(stop_requested_) {
            do_stop_if_able();
        }
    }

    auto base_comch_server::do_stop_if_able() -> void {
        if(active_producers_.active_contexts().empty()) {
            context::stop();
        }
    }

    comch_server::comch_server(
        std::string const &server_name,
        comch_device &dev,
        device_representor &rep,
        comch_server_callbacks callbacks,
        comch_server_limits const &limits
    ):
        base_comch_server { server_name, dev, rep, limits },
        callbacks_ { std::move(callbacks) }
    {}

    auto comch_server::send_completion(
        doca_comch_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_completion) {
            callbacks_.send_completion(*this, task, task_user_data);
        }
    }
    
    auto comch_server::send_error(
        doca_comch_task_send *task,
        doca_data task_user_data
    ) -> void {
        if(callbacks_.send_error) {
            callbacks_.send_error(*this, task, task_user_data);
        }
    }
    
    auto comch_server::msg_recv(
        std::span<std::uint8_t> recv_buffer,
        doca_comch_connection *comch_connection
    ) -> void {
        if(callbacks_.message_received) {
            callbacks_.message_received(*this, recv_buffer, comch_connection);
        }
    }

    auto comch_server::server_connection(
        doca_comch_connection *comch_connection,
        std::uint8_t change_successful
    ) -> void {
        if(callbacks_.server_connection) {
            callbacks_.server_connection(*this, comch_connection, change_successful);
        }
    }

    auto comch_server::server_disconnection(
        doca_comch_connection *comch_connection,
        std::uint8_t change_successful
    ) -> void {
        if(callbacks_.server_disconnection) {
            callbacks_.server_disconnection(*this, comch_connection, change_successful);
        }
    }

    auto comch_server::new_consumer(
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        if(callbacks_.new_consumer) {
            callbacks_.new_consumer(*this, comch_connection, remote_consumer_id);
        }
    }

    auto comch_server::expired_consumer(
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        if(callbacks_.expired_consumer) {
            callbacks_.expired_consumer(*this, comch_connection, remote_consumer_id);
        }
    }
}
