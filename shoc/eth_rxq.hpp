#pragma once

#include "aligned_memory.hpp"
#include "common/accepter_queues.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "coro/value_awaitable.hpp"
#include "device.hpp"
#include "memory_map.hpp"

#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_flow.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>

namespace shoc {
    struct eth_rxq_packet_buffer {
        std::reference_wrapper<memory_map> mmap;
        std::uint32_t offset;
        std::uint32_t length;
    };

    class eth_rxq_packet_memory {
    public:
        eth_rxq_packet_memory(
            std::uint32_t size,
            device dev,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ):
            memory_ { size },
            mmap_ { dev, memory_.as_writable_bytes(), permissions }
        { }

        eth_rxq_packet_memory(
            std::uint32_t size,
            std::initializer_list<device> devs,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ):
            memory_ { size },
            mmap_ { devs, memory_.as_writable_bytes(), permissions }
        { }

        auto as_buffer() -> eth_rxq_packet_buffer {
            return { mmap_, 0, static_cast<std::uint32_t>(memory_.as_bytes().size()) };
        }

    private:
        aligned_memory memory_;
        memory_map mmap_;
    };

    struct eth_rxq_config {
        std::uint32_t max_burst_size;
        std::uint32_t max_packet_size;
        std::optional<std::uint8_t> metadata_num = std::nullopt;
        bool enable_flow_tag = false;
        bool enable_rx_hash = false;
        std::uint16_t packet_headroom = 0;
        std::uint16_t packet_tailroom = 0;
        bool enable_timestamp = false;
        std::optional<std::uint32_t> max_recv_buf_list_len = std::nullopt;
    };

    // TODO: Add support for metadata (flow tag, rx hash, etc)

    class eth_rxq_base:
        public context<
            doca_eth_rxq,
            doca_eth_rxq_destroy,
            doca_eth_rxq_as_doca_ctx
        >
    {
    protected:
        eth_rxq_base(
            context_parent *parent,
            device dev,
            eth_rxq_config const &cfg,
            doca_eth_rxq_type type,
            std::optional<eth_rxq_packet_buffer> pkt_buf
        );

    public:
        auto flow_queue_id() const -> std::uint16_t;

        auto flow_target(
            std::uint32_t outer_flags = 0,
            std::uint32_t inner_flags = 0,
            doca_flow_rss_hash_function rss_hash_func = DOCA_FLOW_RSS_HASH_FUNCTION_TOEPLITZ
        ) -> doca_flow_fwd;

    private:
        device dev_;
        std::uint16_t flow_queue_id_ = std::numeric_limits<std::uint16_t>::max();
    };

    class eth_rxq:
        public eth_rxq_base
    {
    public:
        eth_rxq(
            context_parent *parent,
            device dev,
            std::uint32_t max_tasks,
            eth_rxq_config const &cfg,
            doca_eth_rxq_type type = DOCA_ETH_RXQ_TYPE_REGULAR,
            std::optional<eth_rxq_packet_buffer> pkt_buf = std::nullopt
        );

        auto receive(buffer &dest) const -> coro::status_awaitable<>;
    };

    class eth_rxq_managed:
        public eth_rxq_base
    {
    public:
        eth_rxq_managed(
            context_parent *parent,
            device dev,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf
        );

        auto receive() -> coro::value_awaitable<buffer>;

    private:
        static auto event_managed_recv_callback(
            doca_eth_rxq_event_managed_recv *event,
            struct doca_buf *pkt,
            doca_data user_data
        ) -> void;

        accepter_queues<buffer> managed_queues_;
    };

    class eth_rxq_batch_managed:
        public eth_rxq_base
    {
    public:
        eth_rxq_batch_managed(
            context_parent *parent,
            device dev,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf,
            doca_event_batch_events_number events_number_max = DOCA_EVENT_BATCH_EVENTS_NUMBER_128,
            doca_event_batch_events_number events_number_min = DOCA_EVENT_BATCH_EVENTS_NUMBER_1
        );

        // for managed receive
        auto batch_receive() -> coro::value_awaitable<std::vector<buffer>>;

    private:
        static auto event_batch_managed_recv_callback(
            doca_eth_rxq_event_batch_managed_recv *event,
            std::uint16_t events_number,
            doca_data user_data,
            doca_error_t status,
            struct doca_buf **pkt_array
        ) -> void;

        accepter_queues<std::vector<buffer>> managed_batch_queues_;
    };
}
