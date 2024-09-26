#include "server.hpp"
#include <doca/logger.hpp>

#include <doca_pe.h>

#include <string_view>

namespace doca::comch {
    server_connection::server_connection(
        doca_comch_connection *con,
        server *ctx
    ):
        handle_ { con },
        ctx_ { ctx }
    {
        update_user_data();
    }

    server_connection::~server_connection() {
        if(handle_ != nullptr) {
            auto err = disconnect();

            if(err != DOCA_SUCCESS) {
                logger->warn("disconnection failed in server_connection destructor");
            }
        }
    }

    server_connection::server_connection(server_connection &&other) {
        swap(other);
    }

    server_connection &server_connection::operator=(server_connection &&other) {
        auto copy = server_connection(std::move(other));
        swap(copy);
        return *this;
    }

    auto server_connection::swap(server_connection &other) noexcept -> void {
        std::swap(handle_, other.handle_);
        std::swap(ctx_, other.ctx_);
        std::swap(pending_messages_, other.pending_messages_);
        std::swap(pending_receivers_, other.pending_receivers_);

        update_user_data();
        other.update_user_data();
    }

    auto server_connection::update_user_data() noexcept -> void {
        if(handle_ != nullptr) {
            doca_comch_connection_set_user_data(handle_, (doca_data) { .ptr = this });
        }
    }

    auto server_connection::resolve(doca_comch_connection *handle) -> server_connection* {
        auto user_data = doca_comch_connection_get_user_data(handle);
        return static_cast<server_connection*>(user_data.ptr);
    }

    auto server_connection::disconnect() -> doca_error_t {
        logger->debug("disconnecting");

        if(handle_ == nullptr) {
            return DOCA_ERROR_INVALID_VALUE;
        }

        auto err = doca_comch_server_disconnect(ctx_->handle(), handle_);

        if(err == DOCA_SUCCESS) {
            // just signalling ourselves to avoid code duplication
            signal_disconnect();
        }

        return err;
    }

    auto server_connection::send(std::string_view message) -> status_awaitable {
        if(handle_ == nullptr) {
            return status_awaitable::from_value(DOCA_ERROR_NOT_CONNECTED);
        }

        doca_comch_task_send *send_task;

        auto result = status_awaitable::create_space();
        doca_data task_user_data = { .ptr = result.dest.get() };

        enforce_success(doca_comch_server_task_send_alloc_init(
            ctx_->handle(),
            handle_,
            message.data(),
            message.size(),
            &send_task
        ));

        auto task = doca_comch_task_send_as_task(send_task);
        doca_task_set_user_data(task, task_user_data);

        auto err = doca_task_submit(task);

        if(err != DOCA_SUCCESS) {
            doca_task_free(task);
            throw doca_exception(err);
        }

        return result;
    }

    auto server_connection::msg_recv() -> message_awaitable {
        logger->debug("server connection msg_recv");

        if(!pending_messages_.empty()) {
            logger->debug("using pending message");

            auto result = message_awaitable::from_value(std::move(pending_messages_.front()));
            pending_messages_.pop();
            return result;
        } else if(handle_ == nullptr) {
            logger->debug("disconnected, sending nullopt");
            return message_awaitable::from_value(std::nullopt);
        } else {
            logger->debug("registering receiver for waiting");
            auto result = message_awaitable::create_space();
            pending_receivers_.push(result.dest.get());
            return result;
        }
    }

    auto server_connection::signal_message(std::string_view msg) -> void {
        if(pending_receivers_.empty()) {
            pending_messages_.emplace(msg);
        } else {
            auto receiver = pending_receivers_.front();
            receiver->value = msg;
            pending_receivers_.pop();
            receiver->resume();
        }
    }

    auto server_connection::signal_disconnect() -> void {
        handle_ = nullptr;
    }

    server::server(
        context_parent *parent,
        std::string const &server_name,
        comch_device &dev,
        device_representor &rep,
        server_limits const &limits
    ):
        context { parent }
    {
        doca_comch_server *doca_server;

        enforce_success(doca_comch_server_create(dev.handle(), rep.handle(), server_name.c_str(), &doca_server));
        handle_.reset(doca_server);

        context::init_state_changed_callback();

        enforce_success(doca_comch_server_task_send_set_conf(
            handle_.handle(),
            &server::send_completion_entry,
            &server::send_completion_entry,
            limits.num_send_tasks
        ));
        enforce_success(doca_comch_server_event_msg_recv_register(
            handle_.handle(),
            &server::msg_recv_entry
        ));
        enforce_success(doca_comch_server_event_connection_status_changed_register(
            handle_.handle(),
            &server::connection_entry,
            &server::disconnection_entry
        ));
        //enforce_success(doca_comch_server_event_consumer_register(
        //    handle_.handle(),
        //    &server::new_consumer_entry,
        //    &server::expired_consumer_entry
        //));
        enforce_success(doca_comch_server_set_max_msg_size(
            handle_.handle(),
            limits.max_msg_size
        ));
        enforce_success(doca_comch_server_set_recv_queue_size(
            handle_.handle(),
            limits.recv_queue_size
        ));
    }

    server::~server() {
        assert(get_state() == DOCA_CTX_STATE_IDLE);
    }

    auto server::accept() -> server_connection_awaitable {
        if(pending_connections_.empty()) {
            auto result = server_connection_awaitable::create_space();
            pending_accepters_.push(result.dest.get());
            return result;
        } else {
            auto result = server_connection_awaitable::from_value(std::move(pending_connections_.front()));
            pending_connections_.pop();
            return result;
        }
    }

    auto server::connection_entry(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint8_t change_successful
    ) -> void {
        if(!change_successful) {
            logger->warn("comch::server: unsuccessful connection");
            return;
        }

        auto self = server::resolve(comch_connection);
        if(self == nullptr) {
            logger->error("received connection to unknown server, bailing out");
            return;
        }

        if(self->pending_accepters_.empty()) {
            self->pending_connections_.emplace(comch_connection, self);
        } else {
            auto accepter = self->pending_accepters_.front();
            accepter->value = server_connection(comch_connection, self);
            self->pending_accepters_.pop();
            accepter->resume();
        }
    }

    auto server::disconnection_entry(
        [[maybe_unused]] doca_comch_event_connection_status_changed *event,
        [[maybe_unused]] doca_comch_connection *comch_connection,
        [[maybe_unused]] std::uint8_t change_successful
    ) -> void {
        if(!change_successful) {
            logger->warn("comch::server: unsuccessful disconnection attempt");
            return;
        }

        auto server_con = server_connection::resolve(comch_connection);
        assert(server_con != nullptr);

        server_con->signal_disconnect();
    }

    auto server::send_completion_entry(
        doca_comch_task_send *task,
        [[maybe_unused]] doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        auto base_task = doca_comch_task_send_as_task(task);
        auto status = doca_task_get_status(base_task);

        doca_task_free(base_task);

        auto dest = static_cast<status_awaitable::payload_type*>(task_user_data.ptr);
        dest->value = status;
        dest->resume();
    }

    auto server::msg_recv_entry(
        [[maybe_unused]] doca_comch_event_msg_recv *event,
        std::uint8_t *recv_buffer,
        std::uint32_t msg_len,
        doca_comch_connection *comch_connection
    ) -> void {
        logger->debug("got message for connection {}", static_cast<void*>(comch_connection));

        auto server_con = server_connection::resolve(comch_connection);
        assert(server_con != nullptr);

        auto msg = std::string_view {reinterpret_cast<char const *>(recv_buffer), msg_len };
        server_con->signal_message(msg);
    }

    auto server::resolve(doca_comch_connection *handle) -> server* {
        auto server_handle = doca_comch_server_get_server_ctx(handle);
        return resolve(server_handle);
    }

    auto server::resolve(doca_comch_server *handle) -> server* {
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

        auto base_context = static_cast<context*>(user_data.ptr);
        return static_cast<server*>(base_context);
    }
}
