#include "client.hpp"

#include <shoc/common/raw_memory.hpp>
#include <shoc/logger.hpp>
#include <shoc/progress_engine.hpp>

#include <doca_pe.h>

namespace shoc::comch {
    client::client(
        context_parent *parent,
        std::string const &server_name,
        device dev,
        client_limits const &limits
    ):
        context {
            parent,
            context::create_doca_handle<doca_comch_client_create>(dev.handle(), server_name.c_str())
        },
        dev_ { std::move(dev) }
    {
        enforce(dev_.has_capability(device_capability::comch_client), DOCA_ERROR_NOT_SUPPORTED);

        enforce_success(doca_comch_client_task_send_set_conf(
            handle(),
            &plain_status_callback<doca_comch_task_send_as_task>,
            &plain_status_callback<doca_comch_task_send_as_task>,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_client_event_msg_recv_register(
            handle(),
            &client::msg_recv_callback
        ));
        enforce_success(doca_comch_client_event_consumer_register(
            handle(),
            &client::new_consumer_callback,
            &client::expired_consumer_callback
        ));
        enforce_success(doca_comch_client_set_max_msg_size(handle(), limits.max_msg_size));
        enforce_success(doca_comch_client_set_recv_queue_size(handle(), limits.recv_queue_size));
    }

    client::~client() {
        if(doca_state() != DOCA_CTX_STATE_IDLE) {
            logger->error("client not idle upon destruction, state = {}", static_cast<int>(doca_state()));
        }
    }

    auto client::connection_handle() const -> doca_comch_connection* {
        doca_comch_connection *result;
        enforce_success(doca_comch_client_get_connection(handle(), &result));
        return result;
    }

    auto client::stop() -> context_state_awaitable {
        if(state_ == connection_state::CONNECTED) {
            state_ = connection_state::DISCONNECTING;

            active_children_.stop_all();
            disconnect_if_able();
        }

        return context_state_awaitable { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto client::signal_stopped_child(context_base *stopped_child) -> void {
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

    auto client::send(std::span<std::byte const> message) -> coro::status_awaitable<> {
        return send(reinterpret_span<char const>(message));
    }

    auto client::send(std::span<std::uint8_t const> message) -> coro::status_awaitable<> {
        return send(reinterpret_span<char const>(message));
    }

    auto client::send(char const *message_cstr) -> coro::status_awaitable<> {
        return send(create_span<char const>(message_cstr, std::strlen(message_cstr)));
    }

    auto client::send(std::span<char const> message) -> coro::status_awaitable<> {
        if(state_ != connection_state::CONNECTED) {
            return coro::status_awaitable<>::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_connection *connection;
        auto err = doca_comch_client_get_connection(handle(), &connection);

        if(err != DOCA_SUCCESS) {
            return coro::status_awaitable<>::from_value(err);
        }

        return detail::plain_status_offload<
            doca_comch_client_task_send_alloc_init,
            doca_comch_task_send_as_task
        >(
            engine(),
            handle(),
            connection,
            message.data(),
            message.size()
        );
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

        auto base_context = static_cast<context_base*>(user_data.ptr);
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
        auto client = client::resolve(comch_connection);

        if(client != nullptr) {
            client->remote_consumer_queues_.expire(remote_consumer_id);
        } else {
            logger->error("comch client got new consumer on unknown/expired connection");
        }
    }
}
