#include "rdma.hpp"

#include "common/status.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"

#include <doca_bitfield.h>

#include <optional>

namespace shoc {
    namespace {
        auto get_port_from_addr(doca_rdma_addr *addr) -> std::optional<std::uint16_t> {
            doca_rdma_addr_type type;
            char const *address;
            std::uint16_t port;

            auto err = doca_rdma_addr_get_params(addr, &type, &address, &port);

            if(err != DOCA_SUCCESS) {
                logger->error("Unable to get port from RDMA address: {}", doca_error_get_descr(err));                
                return std::nullopt;
            }

            return port;
        }

        auto get_port_from_connection(doca_rdma_connection *conn) -> std::optional<std::uint16_t> {
            doca_rdma_addr *addr;

            auto err = doca_rdma_connection_get_addr(conn, &addr);

            if(err != DOCA_SUCCESS) {
                logger->error("Unable to get address from RDMA connection: {}", doca_error_get_descr(err));
                return std::nullopt;
            }

            return get_port_from_addr(addr);
        }
    }

    rdma_address::rdma_address(
        doca_rdma_addr_type addr_type,
        char const *address,
        std::uint16_t port
    ) {
        doca_rdma_addr *addr;
        enforce_success(doca_rdma_addr_create(addr_type, address, port, &addr));
        addr_.reset(addr);
    }

    [[nodiscard]]
    auto rdma_address::params() const -> params_type {
        auto result = params_type {};
        enforce_success(doca_rdma_addr_get_params(
            addr_.get(),
            &result.addr_type,
            &result.address,
            &result.port
        ));
        return result;
    }

    rdma_connection::rdma_connection(rdma_context *parent):
        parent_ { parent }
    {
        void const *base = nullptr;
        std::size_t size = 0;

        doca_rdma_connection *conn;
        enforce_success(doca_rdma_export(parent_->handle(), &base, &size, &conn));
        handle_.reset(conn);
        details_ = std::span { static_cast<std::byte const*>(base), size };
    }

    rdma_connection::rdma_connection(
        rdma_context *parent,
        doca_rdma_connection *cm_conn
    ) noexcept:
        parent_ { parent },
        handle_ { cm_conn }
    {}

    auto rdma_connection::connect(std::span<std::byte const> remote_details) -> void {
        enforce_success(doca_rdma_connect(parent_->handle(), remote_details.data(), remote_details.size(), handle_.get()));
    }

    auto rdma_connection::connect(std::string_view remote_details) -> void {
        auto bytes = std::span {
            reinterpret_cast<std::byte const*>(remote_details.data()),
            remote_details.size()
        };
        connect(bytes);
    }

    auto rdma_connection::send(buffer const &src) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_send_allocate_init,
            doca_rdma_task_send_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            src.handle()
        );
    }

    auto rdma_connection::send(buffer const &src, std::uint32_t immediate_data) -> coro::status_awaitable<> {
        auto imm_be32 = DOCA_HTOBE32(immediate_data);

        return detail::plain_status_offload<
            doca_rdma_task_send_imm_allocate_init,
            doca_rdma_task_send_imm_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            src.handle(),
            imm_be32
        );
    }

    auto rdma_connection::receive(buffer &dest, std::uint32_t *immediate_data) -> coro::status_awaitable<std::uint32_t> {
        return detail::status_offload<
            doca_rdma_task_receive_allocate_init,
            doca_rdma_task_receive_as_task
        >(
            parent_->engine(),
            coro::status_awaitable<std::uint32_t>::create_space(immediate_data),
            parent_->handle(),
            dest.handle()
        );
    }

    auto rdma_connection::read(buffer const &src, buffer &dest) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_read_allocate_init,
            doca_rdma_task_read_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            src.handle(),
            dest.handle()
        );
    }

    auto rdma_connection::write(buffer const &src, buffer &dest) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_write_allocate_init,
            doca_rdma_task_write_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            src.handle(),
            dest.handle()
        );
    }

    auto rdma_connection::write(buffer const &src, buffer &dest, std::uint32_t immediate_data) -> coro::status_awaitable<> {
        auto imm_be32 = DOCA_HTOBE32(immediate_data);

        return detail::plain_status_offload<
            doca_rdma_task_write_imm_allocate_init,
            doca_rdma_task_write_imm_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            src.handle(),
            dest.handle(),
            imm_be32
        );
    }

    auto rdma_connection::atomic_cmp_swp(
        buffer dst,
        buffer result,
        std::uint64_t cmp_data,
        std::uint64_t swap_data
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_atomic_cmp_swp_allocate_init,
            doca_rdma_task_atomic_cmp_swp_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            dst.handle(),
            result.handle(),
            cmp_data,
            swap_data
        );
    }

    auto rdma_connection::atomic_fetch_add(
        buffer dst,
        buffer result,
        std::uint64_t add_data
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_atomic_fetch_add_allocate_init,
            doca_rdma_task_atomic_fetch_add_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            dst.handle(),
            result.handle(),
            add_data
        );
    }

    auto rdma_connection::remote_net_sync_event_get(
        sync_event_remote_net const &event,
        buffer dst
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_remote_net_sync_event_get_allocate_init,
            doca_rdma_task_remote_net_sync_event_get_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            event.handle(),
            dst.handle()
        );
    }

    auto rdma_connection::remote_net_sync_event_notify_set(
        sync_event_remote_net const &event,
        buffer src
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_remote_net_sync_event_notify_set_allocate_init,
            doca_rdma_task_remote_net_sync_event_notify_set_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            event.handle(),
            src.handle()
        );
    }

    auto rdma_connection::remote_net_sync_event_notify_add(
        sync_event_remote_net const &event,
        buffer result,
        std::uint64_t add_data
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_remote_net_sync_event_notify_add_allocate_init,
            doca_rdma_task_remote_net_sync_event_notify_add_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_.get(),
            event.handle(),
            result.handle(),
            add_data
        );
    }

    rdma_context::rdma_context(
        context_parent *parent,
        device dev,
        rdma_config config
    ):
        context {
            parent,
            context::create_doca_handle<doca_rdma_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce_success(doca_rdma_set_permissions(handle(), config.rdma_permissions));
        if(config.gid_index.has_value()) {
            enforce_success(doca_rdma_set_gid_index(handle(), config.gid_index.value()));
        }

        enforce_success(doca_rdma_set_connection_state_callbacks(
            handle(),
            &rdma_context::connection_request,
            &rdma_context::connection_established,
            &rdma_context::connection_failure,
            &rdma_context::connection_disconnected
        ));
        enforce_success(doca_rdma_task_receive_set_conf(
            handle(),
            &rdma_context::receive_completion_callback,
            &rdma_context::receive_completion_callback,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_send_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_send_as_task>,
            &plain_status_callback<doca_rdma_task_send_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_send_imm_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_read_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_read_as_task>,
            &plain_status_callback<doca_rdma_task_read_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_write_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_write_as_task>,
            &plain_status_callback<doca_rdma_task_write_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_write_imm_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_cmp_swp_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_fetch_add_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_get_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_set_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_add_set_conf(
            handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            config.max_tasks
        ));
    }

    auto rdma_context::receive_completion_callback(
        doca_rdma_task_receive *task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        assert(task_user_data.ptr != nullptr);

        auto dest = static_cast<typename coro::status_awaitable<std::uint32_t>::payload_type*>(task_user_data.ptr);
        auto base_task = doca_rdma_task_receive_as_task(task);
        auto status = doca_task_get_status(base_task);
        auto imm_be32 = doca_rdma_task_receive_get_result_immediate_data(task);

        doca_task_free(base_task);

        dest->set_value(std::move(status));
        dest->additional_data().overwrite(DOCA_BETOH32(imm_be32));
        dest->resume();
    }

    auto rdma_context::export_connection() -> rdma_connection {
        return { this };
    }

    auto rdma_context::connect(
        rdma_address const &peer
    ) -> coro::value_awaitable<rdma_connection> {
        if(cm_role_ != rdma_cm_role::none) {
            return coro::value_awaitable<rdma_connection>::from_error(DOCA_ERROR_BAD_STATE);
        }

        auto dest = coro::value_awaitable<rdma_connection>::create_space();

        doca_data conn_user_data = { .ptr = dest.receptable_ptr() };
        auto err = doca_rdma_connect_to_addr(handle(), peer.handle(), conn_user_data);

        if(err == DOCA_SUCCESS) {
            cm_role_ = rdma_cm_role::client;
        } else {
            dest.receptable_ptr()->set_error(err);
        }

        return dest;
    }

    auto rdma_context::listen(
        std::uint16_t port
    ) -> coro::value_awaitable<rdma_connection> {
        if(listeners_.contains(port)) {
            return coro::value_awaitable<rdma_connection>::from_error(DOCA_ERROR_ALREADY_EXIST);
        }

        auto err = doca_rdma_start_listen_to_port(handle(), port);
        if(err != DOCA_SUCCESS) {
            return coro::value_awaitable<rdma_connection>::from_error(err);
        }

        cm_role_ = rdma_cm_role::server;
        auto result = coro::value_awaitable<rdma_connection>::create_space();
        listeners_[port] = result.receptable_ptr();
        return result;
    }

    auto rdma_context::connection_request(
        doca_rdma_connection *conn,
        doca_data ctx_user_data
    ) noexcept -> void {
        auto ctx = static_cast<context_base*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);
        assert(rdma->cm_role_ == rdma_cm_role::server);

        auto port = get_port_from_connection(conn);
        if(!port) {
            doca_rdma_connection_reject(conn);
            return;
        }
        
        auto dest_it = rdma->listeners_.find(*port);
        if(dest_it == rdma->listeners_.end()) {
            logger->error("Got RDMA connection request for port we weren't listening on: {}", *port);
            doca_rdma_connection_reject(conn);
            return;
        }
        auto dest = dest_it->second;

        // TODO: mechanism to decide when to accept a connection
        auto err = doca_rdma_connection_accept(conn, nullptr, 0);
        if(err != DOCA_SUCCESS) {
            doca_rdma_connection_reject(conn);
            dest->set_error(err);
            rdma->listeners_.erase(dest_it);
        }
    }

    auto rdma_context::connection_established(
        doca_rdma_connection *conn,
        doca_data conn_user_data,
        doca_data ctx_user_data
    ) noexcept -> void {
        auto ctx = static_cast<context_base*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        auto dest = rdma->take_connection_receptable(conn, conn_user_data);

        if(dest == nullptr) {
            return;
        }

        dest->emplace_value(rdma, conn);
        dest->resume();
    }

    auto rdma_context::connection_failure(
        doca_rdma_connection *conn,
        doca_data conn_user_data,
        doca_data ctx_user_data
    ) noexcept -> void {
        auto ctx = static_cast<context_base*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        auto dest = rdma->take_connection_receptable(conn, conn_user_data);

        if(dest == nullptr) {
            return;
        }

        dest->set_error(DOCA_ERROR_CONNECTION_ABORTED);
        dest->resume();
    }

    auto rdma_context::connection_disconnected(
        [[maybe_unused]] doca_rdma_connection *conn,
        [[maybe_unused]] doca_data conn_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) noexcept -> void { }

    auto rdma_context::take_connection_receptable(
        doca_rdma_connection *conn,
        doca_data conn_user_data
    ) -> coro::value_receptable<rdma_connection>* {
        switch(cm_role_) {
        case rdma_cm_role::server:
        {
            auto port = get_port_from_connection(conn);
            if(!port) {
                return nullptr;
            }

            auto dest_it = listeners_.find(*port);
            if(dest_it == listeners_.end()) {
                return nullptr;
            }

            auto dest = dest_it->second;
            listeners_.erase(dest_it);
            return dest;
        }
        case rdma_cm_role::client:
        {
            auto dest = static_cast<coro::value_receptable<rdma_connection>*>(conn_user_data.ptr);
            doca_data empty = { .ptr = nullptr };
            doca_rdma_connection_set_user_data(conn, empty);
            return dest;
        }
        default:
            return nullptr;
        }
    }
}
