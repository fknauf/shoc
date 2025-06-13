#include "eth_rxq.hpp"

#include "common/status.hpp"
#include "error.hpp"
#include "progress_engine.hpp"

#include <cassert>

namespace shoc {
    namespace {
        template<typename T>
        auto make_optional_value(doca_error_t err, T &data) -> std::optional<T> {
            if(err != DOCA_SUCCESS) {
                return std::nullopt;
            }

            return data;
        }
    }

    eth_rxq_base::eth_rxq_base(
        context_parent *parent,
        device dev,
        eth_rxq_config const &cfg,
        doca_eth_rxq_type type,
        std::optional<eth_rxq_packet_buffer> pkt_buf
    ):
        context {
            parent,
            context::create_doca_handle<doca_eth_rxq_create>(
                dev.handle(),
                cfg.max_burst_size,
                cfg.max_packet_size
            )
        },
        dev_ { dev }
    {
        if(cfg.metadata_num) {
            enforce_success(doca_eth_rxq_set_metadata_num(handle(), *cfg.metadata_num));
        }

        enforce_success(doca_eth_rxq_set_flow_tag(handle(), cfg.enable_flow_tag));
        enforce_success(doca_eth_rxq_set_rx_hash(handle(), cfg.enable_rx_hash));

        if(cfg.packet_headroom) {
            enforce_success(doca_eth_rxq_set_packet_headroom(handle(), *cfg.packet_headroom));
        }

        if(cfg.packet_tailroom) {
            enforce_success(doca_eth_rxq_set_packet_tailroom(handle(), *cfg.packet_tailroom));
        }

        enforce_success(doca_eth_rxq_set_timestamp(handle(), cfg.enable_timestamp));

        if(cfg.max_recv_buf_list_len) {
            enforce_success(doca_eth_rxq_set_max_recv_buf_list_len(handle(), *cfg.max_recv_buf_list_len));
        }

        enforce_success(doca_eth_rxq_set_type(handle(), type));

        if(pkt_buf) {
            if(type == DOCA_ETH_RXQ_TYPE_REGULAR) {
                logger->warn("packet buffer supplied for eth_rxq with type == regular, which will not use it.");
            }

            enforce_success(doca_eth_rxq_set_pkt_buf(
                handle(),
                pkt_buf->mmap.get().handle(),
                pkt_buf->offset,
                pkt_buf->length
            ));
        }

        enforce_success(doca_eth_rxq_get_flow_queue_id(handle(), &flow_queue_id_));
    }

    auto eth_rxq_base::flow_target(
        std::uint32_t outer_flags,
        std::uint32_t inner_flags,
        doca_flow_rss_hash_function rss_hash_func
    ) -> doca_flow_fwd {
        return (doca_flow_fwd) {
            .type = DOCA_FLOW_FWD_RSS,
            .rss_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED,
            .rss = {
                .outer_flags = outer_flags,
                .inner_flags = inner_flags,
                .queues_array = &flow_queue_id_,
                .nr_queues = 1,
                .rss_hash_func = rss_hash_func
            }
        };
    }

    eth_rxq::eth_rxq(
        context_parent *parent,
        device dev,
        std::uint32_t max_tasks,
        eth_rxq_config const &cfg,
        doca_eth_rxq_type type,
        std::optional<eth_rxq_packet_buffer> pkt_buf
    ):
        eth_rxq_base { parent, dev, cfg, type, pkt_buf }
    {
        enforce_success(doca_eth_rxq_task_recv_set_conf(
            handle(),
            plain_status_callback<doca_eth_rxq_task_recv_as_doca_task>,
            plain_status_callback<doca_eth_rxq_task_recv_as_doca_task>,
            max_tasks
        ));
    }

    // for regular, cyclic
    auto eth_rxq::receive(
        buffer &dest
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_eth_rxq_task_recv_allocate_init,
            doca_eth_rxq_task_recv_as_doca_task
        >(
            engine(),
            handle(),
            dest.handle()
        );
    }

    eth_rxq_managed::eth_rxq_managed(
        context_parent *parent,
        device dev,
        eth_rxq_config const &cfg,
        eth_rxq_packet_buffer pkt_buf
    ):
        eth_rxq_base { parent, dev, cfg, DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL, pkt_buf }
    {
        doca_data event_user_data = {
            .ptr = this
        };

        enforce_success(doca_eth_rxq_event_managed_recv_register(
            handle(),
            event_user_data,
            event_managed_recv_callback,
            event_managed_recv_callback
        ));
    }

    auto eth_rxq_managed::receive() -> coro::value_awaitable<buffer> {
        return managed_queues_.accept();
    }

    auto eth_rxq_managed::event_managed_recv_callback(
        [[maybe_unused]] doca_eth_rxq_event_managed_recv *event,
        struct doca_buf *pkt,
        doca_data user_data
    ) -> void {
        auto ctx = static_cast<eth_rxq_managed*>(user_data.ptr);
        auto data = buffer { pkt };
        
        ctx->managed_queues_.supply(data);
    }

    eth_rxq_batch_managed::eth_rxq_batch_managed(
        context_parent *parent,
        device dev,
        eth_rxq_config const &cfg,
        eth_rxq_packet_buffer pkt_buf,
        doca_event_batch_events_number events_number_max,
        doca_event_batch_events_number events_number_min
    ):
        eth_rxq_base { parent, dev, cfg, DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL, pkt_buf }
    {
        enforce_success(doca_eth_rxq_event_batch_managed_recv_register(
            handle(),
            events_number_max,
            events_number_min,
            (doca_data) { .ptr = this },
            event_batch_managed_recv_callback,
            event_batch_managed_recv_callback
        ));
    }

    auto eth_rxq_batch_managed::batch_receive() -> coro::value_awaitable<std::vector<buffer>> {
        return managed_batch_queues_.accept();
    }

    auto eth_rxq_batch_managed::event_batch_managed_recv_callback(
        [[maybe_unused]] doca_eth_rxq_event_batch_managed_recv *event,
        std::uint16_t events_number,
        doca_data user_data,
        doca_error_t status,
        struct doca_buf **pkt_array
    ) -> void {
        if(status != DOCA_SUCCESS) {
            shoc::logger->error("eth_rxq batch receive failed: {}", doca_error_get_descr(status));
            return;
        }

        auto ctx = static_cast<eth_rxq_batch_managed*>(user_data.ptr);

        auto pkt_range = std::span { pkt_array, events_number };
        auto buffers = std::vector<buffer> { pkt_range.begin(), pkt_range.end() };

        ctx->managed_batch_queues_.supply(buffers);
    }
}
