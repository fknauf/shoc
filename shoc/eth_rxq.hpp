#pragma once

#include "aligned_memory.hpp"
#include "common/accepter_queues.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "coro/value_awaitable.hpp"
#include "device.hpp"
#include "memory_map.hpp"
#include "progress_engine.hpp"

#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_flow.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>

/**
 * DOCA Ethernet, receiver queue, see https://docs.nvidia.com/doca/sdk/doca+ethernet/index.html
 *
 * Typically this will be used in conjunction with a flow pipe setup, i.e. one of the rxq classes
 * here will be a RSS forwarding target for a flow pipe. That enables the handling of raw
 * ethernet frames coming through that pipe.
 *
 * We currently split the underlying doca_eth_rxq context into three context classes here, depending
 * on who does the memory management and whether packets are to be processed in batches or not. This
 * is not finalized yet and may be unified in the future.
 *
 * TODO: Work out what's most sensible.
 */
namespace shoc {
    /**
     * Buffer (using an external memory map) to store incoming ethernet frames
     */
    struct eth_rxq_packet_buffer {
        std::reference_wrapper<memory_map> mmap;
        std::uint32_t offset;
        std::uint32_t length;
    };

    /**
     * Buffer (using internal memory and memory map) to store incoming ethernet frames
     */
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

    /**
     * Configuration options for an eth_rxq context, taken directly from DOCA Ethernet
     */
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

    /**
     * Base context for ethernet frame receiver queues
     */
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
            std::uint16_t queue_id,
            eth_rxq_config const &cfg,
            doca_eth_rxq_type type,
            std::optional<eth_rxq_packet_buffer> pkt_buf
        );

    public:
        /**
         * Flow queue ID for use in DOCA Flow
         */
        auto flow_queue_id() const -> std::uint16_t;

        /**
         * Construct forwarding target for DOCA Flow
         * 
         * @param outer_flags flags for the outer packet header
         * @param inner_flags flags for the inner packet header (if packet is part of a tunnel, as in GRE, IPSec)
         * @param rss_hash_func hash function for RSS
         */
        auto flow_target(
            std::uint32_t outer_flags = 0,
            std::uint32_t inner_flags = 0,
            doca_flow_rss_hash_function rss_hash_func = DOCA_FLOW_RSS_HASH_FUNCTION_TOEPLITZ
        ) -> doca_flow_fwd;

    private:
        device dev_;
        std::uint16_t flow_queue_id_ = std::numeric_limits<std::uint16_t>::max();
    };

    /**
     * plain, single-packet eth_rxq context with user-supplied destination buffer
     */
    class eth_rxq:
        public eth_rxq_base
    {
    public:
        eth_rxq(
            context_parent *parent,
            device dev,
            std::uint16_t queue_id,
            std::uint32_t max_tasks,
            eth_rxq_config const &cfg,
            doca_eth_rxq_type type = DOCA_ETH_RXQ_TYPE_REGULAR,
            std::optional<eth_rxq_packet_buffer> pkt_buf = std::nullopt
        );

        [[nodiscard]] static auto create(
            progress_engine_lease &engine,
            device dev,
            std::uint16_t queue_id,
            std::uint32_t max_tasks,
            eth_rxq_config const &cfg,
            doca_eth_rxq_type type = DOCA_ETH_RXQ_TYPE_REGULAR,
            std::optional<eth_rxq_packet_buffer> pkt_buf = std::nullopt
        ) {
            return engine.create_context<eth_rxq>(std::move(dev), queue_id, max_tasks, cfg, type, std::move(pkt_buf));
        }

        /**
         * Receive a single ethernet frame in the supplied destination buffer
         */
        auto receive(buffer &dest) const -> coro::status_awaitable<>;
    };

    /**
     * single-packet receiver queue with library-managed memory buffers
     *
     * received buffers should be returned to the library in a reasonable time frame so they can be reused
     * for newer frames.
     */
    class eth_rxq_managed:
        public eth_rxq_base
    {
    public:
        eth_rxq_managed(
            context_parent *parent,
            device dev,
            std::uint16_t queue_id,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf
        );

        [[nodiscard]] static auto create(
            progress_engine_lease &engine,
            device dev,
            std::uint16_t queue_id,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf
        ) {
            return engine.create_context<eth_rxq_managed>(
                std::move(dev), queue_id, cfg, std::move(pkt_buf)
            );
        }

        /**
         * Receive a single ethernet frame. Memory is managed by DOCA.
         */
        auto receive() -> coro::value_awaitable<buffer>;

    private:
        static auto event_managed_recv_callback(
            doca_eth_rxq_event_managed_recv *event,
            struct doca_buf *pkt,
            doca_data user_data
        ) -> void;

        accepter_queues<buffer> managed_queues_;
    };

    /**
     * eth_rxq queue for batch reception with library-managed memory
     *
     * received buffers should be returned to the library in a reasonable time frame so they can be reused
     * for newer frames.
     */
    class eth_rxq_batch_managed:
        public eth_rxq_base
    {
    public:
        eth_rxq_batch_managed(
            context_parent *parent,
            device dev,
            std::uint16_t queue_id,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf,
            doca_event_batch_events_number events_number_max = DOCA_EVENT_BATCH_EVENTS_NUMBER_128,
            doca_event_batch_events_number events_number_min = DOCA_EVENT_BATCH_EVENTS_NUMBER_1
        );

        [[nodiscard]] static auto create(
            progress_engine_lease &engine,
            device dev,
            std::uint16_t queue_id,
            eth_rxq_config const &cfg,
            eth_rxq_packet_buffer pkt_buf,
            doca_event_batch_events_number events_number_max = DOCA_EVENT_BATCH_EVENTS_NUMBER_128,
            doca_event_batch_events_number events_number_min = DOCA_EVENT_BATCH_EVENTS_NUMBER_1
        ) {
            return engine.create_context<eth_rxq_batch_managed>(
                std::move(dev), queue_id, cfg, std::move(pkt_buf), events_number_max, events_number_min
            );
        }

        /**
         * Receive a batch of ethernet frames. Memory is handled by DOCA
         */
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
