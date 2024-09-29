#include "client.hpp"

#include <doca/logger.hpp>

#include <doca_pe.h>

namespace doca::comch {
    client::client(
        context_parent *parent,
        std::string const &server_name,
        comch_device &dev,
        client_limits const &limits
    ):
        context { parent }
    {
        doca_comch_client *doca_client;

        enforce_success(doca_comch_client_create(dev.handle(), server_name.c_str(), &doca_client));
        handle_.reset(doca_client);

        context::init_state_changed_callback();

        enforce_success(doca_comch_client_task_send_set_conf(
            handle_.handle(),
            &client::send_completion_entry,
            &client::send_completion_entry,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_client_event_msg_recv_register(
            handle_.handle(),
            &client::msg_recv_entry
        ));
        enforce_success(doca_comch_client_event_consumer_register(
            handle_.handle(),
            &client::new_consumer_entry,
            &client::expired_consumer_entry
        ));
        enforce_success(doca_comch_client_set_max_msg_size(handle_.handle(), limits.max_msg_size));
        enforce_success(doca_comch_client_set_recv_queue_size(handle_.handle(), limits.recv_queue_size));
    }

    client::~client() {
        //assert(get_state() == DOCA_CTX_STATE_IDLE);

        if(get_state() != DOCA_CTX_STATE_IDLE) {
            logger->error("client not idle upon destruction, state = {}", get_state());
        }
    }

    auto client::as_ctx() const -> doca_ctx* {
        return doca_comch_client_as_ctx(handle_.handle());
    }

    auto client::connection_handle() const -> doca_comch_connection* {
        doca_comch_connection *result;
        enforce_success(doca_comch_client_get_connection(handle_.handle(), &result));
        return result;
    }

    auto client::send(std::string_view message) -> status_awaitable {
        if(get_state() != DOCA_CTX_STATE_RUNNING) {
            return status_awaitable::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_connection *connection;
        doca_comch_task_send *task;

        auto result = status_awaitable::create_space();
        doca_data task_user_data = { .ptr = result.dest.get() };

        enforce_success(doca_comch_client_get_connection(handle_.handle(), &connection));
        enforce_success(doca_comch_client_task_send_alloc_init(handle_.handle(), connection, message.data(), message.size(), &task));
        doca_task_set_user_data(doca_comch_task_send_as_task(task), task_user_data);

        auto base_task = doca_comch_task_send_as_task(task);
        auto err = doca_task_submit(base_task);

        if(err != DOCA_SUCCESS) {
            doca_task_free(base_task);
            return status_awaitable::from_value(std::move(err));
        }

        return result;
    }

    auto client::msg_recv() -> message_awaitable {
        return message_queues_.accept();
    }

    auto client::send_completion_entry(
        [[maybe_unused]] doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_task = doca_comch_task_send_as_task(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        auto dest = static_cast<status_awaitable::payload_type*>(task_user_data.ptr);
        dest->emplace_value(status);
        dest->resume();
    }

    auto client::msg_recv_entry(
        [[maybe_unused]] doca_comch_event_msg_recv *event,
        std::uint8_t *recv_buffer,
        std::uint32_t msg_len,
        doca_comch_connection *comch_connection
    ) -> void {
        auto self = client::resolve(comch_connection);

        if(self == nullptr) {
            logger->error("received message to unknown client, bailing out");
            return;
        }

        auto msg = std::string_view { reinterpret_cast<char const *>(recv_buffer), msg_len };
        self->message_queues_.supply(message { msg });
    }

    auto client::resolve(doca_comch_connection *handle) -> client* {
        auto server_handle = doca_comch_client_get_client_ctx(handle);
        return resolve(server_handle);
    }

    auto client::resolve(doca_comch_client *handle) -> client* {
        if(handle == nullptr) {
            return nullptr;
        }

        auto ctx = doca_comch_client_as_ctx(handle);

        doca_data user_data;
        auto err = doca_ctx_get_user_data(ctx, &user_data);

        if(err != DOCA_SUCCESS) {
            auto errmsg = doca_error_get_name(err);
            logger->error("comch::server::resolve: could not get user data from ctx, errmsg = {}", errmsg);
            return nullptr;
        }

        auto base_context = static_cast<context*>(user_data.ptr);
        return static_cast<client*>(base_context);
    }

    auto client::state_changed(
        [[maybe_unused]] doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        if(next_state == DOCA_CTX_STATE_IDLE) {
            message_queues_.disconnect();
            remote_consumer_queues_.disconnect();
        }
    }

    auto client::new_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        auto client = client::resolve(comch_connection);

        if(client != nullptr) {
            client->remote_consumer_queues_.supply(remote_consumer_id);
        } else {
            logger->error("comch client got new consumer on unknown/expired connection");
        }
    }

    auto client::expired_consumer_entry(
        [[maybe_unused]] doca_comch_event_consumer *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint32_t remote_consumer_id
    ) -> void {
        // TODO: design and implement logic for consumer expiry
    }
}
