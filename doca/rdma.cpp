#include "rdma.hpp"

#include "common/status.hpp"
#include "progress_engine.hpp"

#include <endian.h>

namespace doca {
    rdma_context::rdma_context(
        context_parent *parent,
        device &dev,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        doca_rdma *rdma;
        enforce_success(doca_rdma_create(dev.handle(), &rdma));
        handle_.reset(rdma);

        init_state_changed_callback();

        enforce_success(doca_rdma_set_connection_state_callbacks(
            handle_.handle(),
            &rdma_context::connection_request,
            &rdma_context::connection_established,
            &rdma_context::connection_failure,
            &rdma_context::connection_disconnected
        ));
        enforce_success(doca_rdma_task_receive_set_conf(
            handle_.handle(),
            &rdma_context::receive_completion_callback,
            &rdma_context::receive_completion_callback,
            max_tasks
        ));
        enforce_success(doca_rdma_task_send_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_send_as_task>,
            &plain_status_callback<doca_rdma_task_send_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_send_imm_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            &plain_status_callback<doca_rdma_task_send_imm_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_read_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_read_as_task>,
            &plain_status_callback<doca_rdma_task_read_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_write_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_write_as_task>,
            &plain_status_callback<doca_rdma_task_write_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_write_imm_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            &plain_status_callback<doca_rdma_task_write_imm_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_cmp_swp_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_cmp_swp_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_atomic_fetch_add_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            &plain_status_callback<doca_rdma_task_atomic_fetch_add_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_get_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_get_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_set_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_set_as_task>,
            max_tasks
        ));
        enforce_success(doca_rdma_task_remote_net_sync_event_notify_add_set_conf(
            handle_.handle(),
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            &plain_status_callback<doca_rdma_task_remote_net_sync_event_notify_add_as_task>,
            max_tasks
        ));
    }

    auto rdma_context::receive(buffer &dest, std::uint32_t *immediate_data) -> coro::status_awaitable<std::uint32_t> {
        return detail::status_offload<
            doca_rdma_task_receive_allocate_init,
            doca_rdma_task_receive_as_task
        >(
            engine(),
            coro::status_awaitable<std::uint32_t>::create_space(immediate_data),
            handle_.handle(),
            dest.handle()
        );
    }

    auto rdma_context::send(buffer const &src) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_send_allocate_init,
            doca_rdma_task_send_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle()
        );
    }

    auto rdma_context::send(buffer const &src, std::uint32_t immediate_data) -> coro::status_awaitable<> {
        auto imm_be32 = htobe32(immediate_data);

        return detail::plain_status_offload<
            doca_rdma_task_send_imm_allocate_init,
            doca_rdma_task_send_imm_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle(),
            imm_be32
        );
    }

    auto rdma_context::read(buffer const &src, buffer &dest) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_read_allocate_init,
            doca_rdma_task_read_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle(),
            dest.handle()
        );
    }

    auto rdma_context::write(buffer const &src, buffer &dest) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_rdma_task_write_allocate_init,
            doca_rdma_task_write_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle(),
            dest.handle()
        );
    }

    auto rdma_context::write(buffer const &src, buffer &dest, std::uint32_t immediate_data) -> coro::status_awaitable<> {
        auto imm_be32 = htobe32(immediate_data);

        return detail::plain_status_offload<
            doca_rdma_task_write_imm_allocate_init,
            doca_rdma_task_write_imm_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle(),
            dest.handle(),
            imm_be32
        );
    }

    auto rdma_context::connection_request(doca_rdma_connection *conn, doca_data ctx_user_data) -> void {
    }

    auto rdma_context::connection_established(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void {
    }

    auto rdma_context::connection_failure(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void {
    }

    auto rdma_context::connection_disconnected(doca_rdma_connection *conn, doca_data conn_user_data, doca_data ctx_user_data) -> void {
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
        dest->additional_data().overwrite(be32toh(imm_be32));
        dest->resume();
    }
}
