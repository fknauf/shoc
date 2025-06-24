#include "eth_txq.hpp"

#include "progress_engine.hpp"

namespace shoc {
    eth_txq::eth_txq(
        context_parent *parent,
        device dev,
        std::uint32_t max_tasks,
        eth_txq_config &cfg
    ):
        context {
            parent,
            context::create_doca_handle<doca_eth_txq_create>(
                dev.handle(),
                cfg.max_burst_size
            )
        }
    {
        enforce_success(doca_eth_txq_set_max_send_buf_list_len(handle(), cfg.max_send_buf_list_len));
        enforce_success(doca_eth_txq_set_metadata_num(handle(), cfg.metadata_num));
        enforce_success(doca_eth_txq_set_mss(handle(), cfg.mss));
        enforce_success(doca_eth_txq_set_max_lso_header_size(handle(), cfg.max_lso_header_size));
        enforce_success(doca_eth_txq_set_type(handle(), cfg.type));
        enforce_success(doca_eth_txq_set_l3_chksum_offload(handle(), cfg.l3_chksum_offload));
        enforce_success(doca_eth_txq_set_l4_chksum_offload(handle(), cfg.l4_chksum_offload));

        if(cfg.wait_on_time_offload) {
            enforce_success(doca_eth_txq_set_wait_on_time_offload(handle()));
        }

        enforce_success(doca_eth_txq_task_send_set_conf(
            handle(),
            plain_status_callback<doca_eth_txq_task_send_as_doca_task>,
            plain_status_callback<doca_eth_txq_task_send_as_doca_task>,
            max_tasks
        ));

        enforce_success(doca_eth_txq_task_lso_send_set_conf(
            handle(),
            plain_status_callback<doca_eth_txq_task_lso_send_as_doca_task>,
            plain_status_callback<doca_eth_txq_task_lso_send_as_doca_task>,
            max_tasks
        ));
    }

    auto eth_txq::send(buffer &pkt) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_eth_txq_task_send_allocate_init,
            doca_eth_txq_task_send_as_doca_task
        >(
            engine(),
            handle(),
            pkt.handle()
        );
    }

    auto eth_txq::lso_send(buffer &payload, doca_gather_list *headers) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_eth_txq_task_lso_send_allocate_init,
            doca_eth_txq_task_lso_send_as_doca_task
        >(
            engine(),
            handle(),
            payload.handle(),
            headers
        );
    }
}
