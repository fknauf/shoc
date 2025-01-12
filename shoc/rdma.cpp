#include "rdma.hpp"

#include "common/status.hpp"
#include "progress_engine.hpp"

#include <doca_bitfield.h>

namespace shoc {
    rdma_connection::rdma_connection(rdma_context *parent):
        parent_ { parent }
    {
        void const *base = nullptr;
        std::size_t size = 0;

        enforce_success(doca_rdma_export(parent_->handle(), &base, &size, &handle_));
        details_ = std::span { static_cast<std::byte const*>(base), size };

        doca_data conn_user_data = { .ptr = this };
        enforce_success(doca_rdma_connection_set_user_data(handle_, conn_user_data));
    }

    auto rdma_connection::connect(std::span<std::byte const> remote_details) -> void {
        enforce_success(doca_rdma_connect(parent_->handle(), remote_details.data(), remote_details.size(), handle_));
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
            handle_,
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
            handle_,
            src.handle(),
            imm_be32
        );
    }

    auto rdma_connection::read(buffer const &src, buffer &dest) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_read_allocate_init,
            doca_rdma_task_read_as_task
        >(
            parent_->engine(),
            parent_->handle(),
            handle_,
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
            handle_,
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
            handle_,
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
            handle_,
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
            handle_,
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
            handle_,
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
            handle_,
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
            handle_,
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
        context { parent },
        dev_ { std::move(dev) }
    {
        doca_rdma *rdma;
        enforce_success(doca_rdma_create(dev_.handle(), &rdma));
        handle_.reset(rdma);

        init_state_changed_callback();

        enforce_success(doca_rdma_set_permissions(handle_.get(), config.rdma_permissions));
        if(config.gid_index.has_value()) {
            enforce_success(doca_rdma_set_gid_index(handle_.get(), config.gid_index.value()));
        }

        //enforce_success(doca_rdma_set_connection_state_callbacks(
        //    handle_.get(),
        //    &rdma_context::connection_request,
        //    &rdma_context::connection_established,
        //    &rdma_context::connection_failure,
        //    &rdma_context::connection_disconnected
        //));
        enforce_success(doca_rdma_task_receive_set_conf(
            handle_.get(),
            &rdma_context::receive_completion_callback,
            &rdma_context::receive_completion_callback,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_send_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_send_as_task>,
            &plain_status_callback<doca_rdma_task_send_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_send_imm_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_read_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_read_as_task>,
            &plain_status_callback<doca_rdma_task_read_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_write_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_write_as_task>,
            &plain_status_callback<doca_rdma_task_write_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_write_imm_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_cmp_swp_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_fetch_add_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_get_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_set_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            config.max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_add_set_conf(
            handle_.get(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            config.max_tasks
        ));
    }

    auto rdma_context::receive(buffer &dest, std::uint32_t *immediate_data) -> coro::status_awaitable<std::uint32_t> {
        return detail::status_offload<
            doca_rdma_task_receive_allocate_init,
            doca_rdma_task_receive_as_task
        >(
            engine(),
            coro::status_awaitable<std::uint32_t>::create_space(immediate_data),
            handle_.get(),
            dest.handle()
        );
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

/*
    auto rdma_context::connect(
        doca_rdma_addr_type address_type,
        char const *address,
        std::uint16_t port
    ) -> coro::status_awaitable<> {
        doca_rdma_addr *addr = nullptr;

        auto err = doca_rdma_addr_create(address_type, address, port, &addr);

        if(err != DOCA_SUCCESS) {
            
        }
    }
 
    auto rdma_context::accept_connection(
        std::uint16_t port
    ) -> coro::status_awaitable<> {
        auto err = doca_rdma_listen_to_port(handle_.get(), port);

        if(err != DOCA_SUCCESS) {
            return coro::status_awaitable<>::from_value(err);
        }

        auto result = coro::status_awaitable<>::create_space();
        accept_receptable_ = result.receptable_ptr();
        return result;
    }

    auto rdma_context::connection_request(
        doca_rdma_connection *conn,
        doca_data ctx_user_data
    ) -> void {
        auto ctx = static_cast<context*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        assert(!rdma->connected_);
        assert(rdma->accept_receptable_ != nullptr);
    
        // TODO: mechanism to decide when to accept a connection
        auto err = doca_rdma_connection_accept(conn);

        if(err != DOCA_SUCCESS) {
            rdma->accept_receptable_->set_error(err);
        }
    }

    auto rdma_context::connection_established(
        doca_rdma_connection *conn,
        [[maybe_unused]] doca_data conn_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto ctx = static_cast<context*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        assert(!rdma->connected_);
        assert(rdma->accept_receptable_ != nullptr);
    
        rdma->connected_ = true;
        rdma->accept_receptable_->set_value(DOCA_SUCCESS);
    }

    auto rdma_context::connection_failure(
        doca_rdma_connection *conn,
        doca_data conn_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto ctx = static_cast<context*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        assert(!rdma->connected_);
        assert(rdma->accept_receptable_ != nullptr);
    
        rdma->accept_receptable_->set_error(DOCA_ERROR_CONNECTION_ABORTED);    
    }

    auto rdma_context::connection_disconnected(
        doca_rdma_connection *conn,
        doca_data conn_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto ctx = static_cast<context*>(ctx_user_data.ptr);
        auto rdma = static_cast<rdma_context*>(ctx);

        assert(rdma->connected_);

        rdma->connected_ = false;
    }
 */
}
