#include "server.hpp"

#include <shoc/common/raw_memory.hpp>
#include <shoc/common/status.hpp>
#include <shoc/logger.hpp>

#include <doca_pe.h>

#include <string_view>

namespace shoc::comch {
    server_connection::server_connection(
        doca_comch_connection *con,
        server *ctx
    ):
        handle_ { con },
        ctx_ { ctx },
        state_ { connection_state::CONNECTED }
    { }

    server_connection::~server_connection() {
        // Either client code disconnected us, or server does when it is stopped -- at the very
        // latest when the progress engine is destroyed. So by the time we get here, the connection
        // should no longer be active.
        if(state_ != connection_state::DISCONNECTED) {
            logger->error("server_connection destructed before it is disconnected");
        }
    }

    auto server_connection::accept_consumer() -> id_awaitable {
        return remote_consumer_queues_.accept();
    }

    auto server_connection::signal_new_consumer(std::uint32_t consumer_id) -> void {
        remote_consumer_queues_.supply(consumer_id);
    }

    auto server_connection::send(std::span<std::byte const> message) -> coro::status_awaitable<> {
        return send(reinterpret_span<char const>(message));
    }

    auto server_connection::send(std::span<std::uint8_t const> message) -> coro::status_awaitable<> {
        return send(reinterpret_span<char const>(message));
    }

    auto server_connection::send(char const *message_cstr) -> coro::status_awaitable<> {
        return send(create_span<char const>(message_cstr, std::strlen(message_cstr)));
    }

    auto server_connection::send(std::span<char const> message) -> coro::status_awaitable<> {
        if(state_ != connection_state::CONNECTED) {
            return coro::status_awaitable<>::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_task_send *send_task;

        auto result = coro::status_awaitable<>::create_space();
        auto receptable = result.receptable_ptr();

        doca_data task_user_data = { .ptr = receptable };

        enforce_success(doca_comch_server_task_send_alloc_init(
            ctx_->handle(),
            handle_,
            message.data(),
            message.size(),
            &send_task
        ));

        auto task = doca_comch_task_send_as_task(send_task);
        doca_task_set_user_data(task, task_user_data);

        engine()->submit_task(task, receptable);

        return result;
    }

    auto server_connection::msg_recv() -> message_awaitable {
        return message_queues_.accept();
    }

    auto server_connection::signal_message(std::string_view msg) -> void {
        message_queues_.supply(message { msg });
    }

    auto server_connection::disconnect() -> server_disconnect_awaitable {
        // We may have child consumers/producers that keep us from disconnecting immediately.
        // So here we mark ourselves disconnecting, ask all children to stop, then disconnect
        // if all children are already stopped.
        //
        // If all children are not stopped yet, disconnect_if_able will not do anything, and
        // instead we rely on all our children to alert us that they've stopped, then when the
        // last one stopped we'll disconnect. See signal_stopped_child

        if(state_ == connection_state::CONNECTED) {
            state_ = connection_state::DISCONNECTING;

            active_children_.stop_all();
            disconnect_if_able();
        }

        return server_disconnect_awaitable { this };
    }

    auto server_connection::disconnect_if_able() -> void {
        assert(handle_ != nullptr);

        // if we still have active children, we can't disconnect yet.
        if(!active_children_.empty()) {
            return;
        }

        logger->debug("disconnecting server_connection {}", static_cast<void*>(handle_));

        auto err = doca_comch_server_disconnect(ctx_->handle(), handle_);

        if(err == DOCA_SUCCESS) {
            // just signalling ourselves to avoid code duplication
            signal_disconnect();
        } else {
            logger->error("could not disconnect server connection {}: {}", static_cast<void*>(handle_), doca_error_get_descr(err));
        }
    }

    auto server_connection::signal_stopped_child(context_base *stopped_child) -> void {
        active_children_.remove_stopped_context(stopped_child);
        if(state_ == connection_state::DISCONNECTING) {
            disconnect_if_able();
        }
    }

    auto server_connection::signal_disconnect() -> void {
        // stuff to do when we're disconnected

        if(state_ == connection_state::DISCONNECTED) {
            logger->warn("server_connection marked disconnected twice");
        }

        // mark ourselves disconnected
        state_ = connection_state::DISCONNECTED;

        // mark queues disconnected. This will cause all waiting accepters to throw
        // an error.
        message_queues_.disconnect();
        remote_consumer_queues_.disconnect();

        auto waiting_coro = std::exchange(coro_disconnect_, nullptr);

        // tell our server we're disconnected so it'll remove us from its connection registry.
        // this may delete us.
        ctx_->signal_disconnect(handle_);

        if(waiting_coro) {
            waiting_coro.resume();
        }
    }

    auto server_connection::engine() -> progress_engine* {
        return ctx_->engine();
    }

    server::server(
        context_parent *parent,
        std::string const &server_name,
        device dev,
        device_representor rep,
        server_limits const &limits
    ):
        context {
            parent,
            context::create_doca_handle<doca_comch_server_create>(
                dev.handle(),
                rep.handle(),
                server_name.c_str()
            )
        },
        dev_ { std::move(dev) },
        rep_ { std::move(rep) }
    {
        enforce(dev_.has_capability(device_capability::comch_server), DOCA_ERROR_NOT_SUPPORTED);
        open_connections_.max_load_factor(0.75);

        enforce_success(doca_comch_server_task_send_set_conf(
            handle(),
            &plain_status_callback<doca_comch_task_send_as_task>,
            &plain_status_callback<doca_comch_task_send_as_task>,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_server_event_msg_recv_register(
            handle(),
            &server::msg_recv_callback
        ));
        enforce_success(doca_comch_server_event_connection_status_changed_register(
            handle(),
            &server::connection_callback,
            &server::disconnection_callback
        ));
        enforce_success(doca_comch_server_event_consumer_register(
            handle(),
            &server::new_consumer_callback,
            &server::expired_consumer_callback
        ));
        enforce_success(doca_comch_server_set_max_msg_size(
            handle(),
            limits.max_msg_size
        ));
        enforce_success(doca_comch_server_set_recv_queue_size(
            handle(),
            limits.recv_queue_size
        ));
    }

    auto server::stop() -> context_state_awaitable {
        // Server has child contexts, so mark as stopping, tell all open connections to
        // disconnect (which will also stop their child consumers/producers), then stop
        // if able, otherwise wait for children to stop before stopping ourselves.
        stop_requested_ = true;

        auto next = open_connections_.begin();

        while(next != open_connections_.end()) {
            // disconnect may remove *it from open_connections_ and invalidate it,
            // but this way next remains valid in that case.
            auto it = next;
            ++next;

            static_cast<void>(it->second->disconnect());
        }

        do_stop_if_able();
        return context_state_awaitable { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto server::do_stop_if_able() -> void {
        if(open_connections_.empty()) {
            auto err = doca_ctx_stop(as_ctx());

            if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
                logger->error("unable to stop comch server {}: {}", static_cast<void*>(handle()), doca_error_get_descr(err));
            }
        }
    }

    auto server::signal_disconnect(doca_comch_connection *con) -> void {
        // child connection disconnected -> remove from registry. If we've been asked to stop, try
        // to stop afterwards.
        auto count_erased = open_connections_.erase(con);

        if(count_erased == 0) {
            logger->error("comch server {} got disconnect signal for unknown connection {}",
                static_cast<void*>(handle()), static_cast<void*>(con));
        }

        if(stop_requested_) {
            do_stop_if_able();
        }
    }

    auto server::accept() -> server_connection_awaitable {
        return connection_queues_.accept();
    }

    auto server::state_changed(
        [[maybe_unused]] doca_ctx_states prev_state,
        doca_ctx_states next_state
    ) -> void {
        if(next_state == DOCA_CTX_STATE_IDLE) {
            // server stopped -> tell all waiting accepters that no more connections will
            // be forthcoming. Anyone co_awaiting one of them will get an exception.
            connection_queues_.disconnect();
        }
    }

    auto server::connection_callback(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint8_t change_successful
    ) -> void {
        if(!change_successful) {
            logger->warn("comch::server: unsuccessful connection");
            return;
        }

        auto self = server::resolve_server(comch_connection);
        if(self == nullptr) {
            logger->error("received connection to unknown server, bailing out");
            return;
        }

        auto new_connection = std::make_shared<server_connection>(comch_connection, self);
        self->open_connections_[comch_connection] = new_connection;
        self->connection_queues_.supply(new_connection);
    }

    auto server::disconnection_callback(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint8_t change_successful
    ) -> void {
        logger->trace("comch server disconnect, con = {}", static_cast<void*>(comch_connection));

        if(!change_successful) {
            logger->warn("comch::server: unsuccessful disconnection attempt");
            return;
        }

        auto server_con = server::resolve_connection(comch_connection);

        if(server_con != nullptr) {
            // client has disconnected from us, so the connection is already disconnected. Signal that to the server_connection.
            server_con->signal_disconnect();
        } else {
            logger->warn("comch server received disconnection event for unknown server_connection {}", static_cast<void*>(comch_connection));
        }
    }

    auto server::msg_recv_callback(
        [[maybe_unused]] doca_comch_event_msg_recv *event,
        std::uint8_t *recv_buffer,
        std::uint32_t msg_len,
        doca_comch_connection *comch_connection
    ) -> void {
        logger->debug("got message for connection {}", static_cast<void*>(comch_connection));

        auto server_con = server::resolve_connection(comch_connection);

        if(server_con != nullptr) {
            auto msg = std::string_view {reinterpret_cast<char const *>(recv_buffer), msg_len };
            server_con->signal_message(msg);
        } else {
            logger->error("comch server got message on unknown/expired connection");
        }
    }

    auto server::new_consumer_callback(
        [[maybe_unused]] doca_comch_event_consumer *event,
        doca_comch_connection *comch_connection,
        std::uint32_t remote_consumer_id
    ) -> void {
        auto con = server::resolve_connection(comch_connection);

        if(con != nullptr) {
            con->signal_new_consumer(remote_consumer_id);
        } else {
            logger->error("comch server got new consumer on unknown/expired connection");
        }
    }

    auto server::expired_consumer_callback(
        [[maybe_unused]] doca_comch_event_consumer *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint32_t remote_consumer_id
    ) -> void {
        // TODO: design and implement logic for consumer expiry
    }

    auto server::resolve_server(doca_comch_connection *handle) -> server* {
        auto server_handle = doca_comch_server_get_server_ctx(handle);
        return resolve_server(server_handle);
    }

    auto server::resolve_server(doca_comch_server *handle) -> server* {
        if(handle == nullptr) {
            return nullptr;
        }

        auto ctx = doca_comch_server_as_ctx(handle);

        doca_data user_data;
        auto err = doca_ctx_get_user_data(ctx, &user_data);

        if(err != DOCA_SUCCESS) {
            auto errmsg = doca_error_get_name(err);
            logger->error("comch::server::resolve: could not get user data from ctx, errmsg = {}", errmsg);
            return nullptr;
        }

        auto base_context = static_cast<context_base*>(user_data.ptr);
        return static_cast<server*>(base_context);
    }

    auto server::resolve_connection(doca_comch_connection *handle) -> server_connection* {
        auto server = resolve_server(handle);

        assert(server != nullptr);

        auto it = server->open_connections_.find(handle);

        if(it == server->open_connections_.end()) {
            return nullptr;
        }

        return it->second.get();
    }

    auto server_disconnect_awaitable::await_ready() const noexcept -> bool {
        return con_->state_ == connection_state::DISCONNECTED;
    }

    auto server_disconnect_awaitable::await_suspend(std::coroutine_handle<> handle) const noexcept -> void {
        con_->coro_disconnect_ = handle;
    }
}
