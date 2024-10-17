#include "client.hpp"

#include <doca/common/status.hpp>
#include <doca/logger.hpp>

#include <doca_pe.h>

namespace doca::comch {
    client::client(
        context_parent *parent,
        std::string const &server_name,
        device const &dev,
        client_limits const &limits
    ):
        context { parent }
    {
        enforce(dev.has_capability(device_capability::comch_client), DOCA_ERROR_NOT_SUPPORTED);

        doca_comch_client *doca_client;

        enforce_success(doca_comch_client_create(dev.handle(), server_name.c_str(), &doca_client));
        handle_.reset(doca_client);

        context::init_state_changed_callback();

        enforce_success(doca_comch_client_task_send_set_conf(
            handle_.handle(),
            &plain_status_callback_function<doca_comch_task_send, doca_comch_task_send_as_task>,
            &plain_status_callback_function<doca_comch_task_send, doca_comch_task_send_as_task>,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_client_event_msg_recv_register(
            handle_.handle(),
            &client::msg_recv_callback
        ));
        enforce_success(doca_comch_client_event_consumer_register(
            handle_.handle(),
            &client::new_consumer_callback,
            &client::expired_consumer_callback
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

    auto client::as_ctx() const noexcept -> doca_ctx* {
        return doca_comch_client_as_ctx(handle_.handle());
    }

    auto client::connection_handle() const -> doca_comch_connection* {
        doca_comch_connection *result;
        enforce_success(doca_comch_client_get_connection(handle_.handle(), &result));
        return result;
    }

    auto client::stop() -> context_state_awaitable {
        if(state_ == connection_state::CONNECTED) {
            state_ = connection_state::DISCONNECTING;
        }

        active_children_.stop_all();
        disconnect_if_able();

        return context_state_awaitable { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto client::signal_stopped_child(context *stopped_child) -> void {
        active_children_.remove_stopped_context(stopped_child);
        if(state_ == connection_state::DISCONNECTING) {
            disconnect_if_able();
        }
    }

    auto client::disconnect_if_able() -> void {
        if(!active_children_.empty()) {
            return;
        }

        auto err = doca_ctx_stop(as_ctx());

        if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
            logger->error("could not stop comch client even though it has no active consumers/producers: {}", doca_error_get_descr(err));
        }
    }

    auto client::send(std::string_view message) -> coro::status_awaitable<> {
        if(state_ != connection_state::CONNECTED) {
            return coro::status_awaitable<>::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_connection *connection;
        doca_comch_task_send *task;

        auto result = coro::status_awaitable<>::create_space();
        auto receptable = result.receptable_ptr();

        doca_data task_user_data = { .ptr = receptable };

        enforce_success(doca_comch_client_get_connection(handle_.handle(), &connection));
        enforce_success(doca_comch_client_task_send_alloc_init(handle_.handle(), connection, message.data(), message.size(), &task));
        doca_task_set_user_data(doca_comch_task_send_as_task(task), task_user_data);

        auto base_task = doca_comch_task_send_as_task(task);
        engine()->submit_task(base_task, receptable);

        return result;
    }

    auto client::msg_recv() -> message_awaitable {
        return message_queues_.accept();
    }

    auto client::msg_recv_callback(
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
        if(next_state == DOCA_CTX_STATE_RUNNING) {
            state_ = connection_state::CONNECTED;
        } else if(next_state == DOCA_CTX_STATE_STOPPING) {
            state_ = connection_state::DISCONNECTING;
        } else if(next_state == DOCA_CTX_STATE_IDLE) {
            message_queues_.disconnect();
            remote_consumer_queues_.disconnect();

            state_ = connection_state::DISCONNECTED;
        }
    }

    auto client::new_consumer_callback(
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

    auto client::expired_consumer_callback(
        [[maybe_unused]] doca_comch_event_consumer *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint32_t remote_consumer_id
    ) -> void {
        // TODO: design and implement logic for consumer expiry
    }
}
