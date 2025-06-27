#pragma once

#include "aligned_memory.hpp"
#include "common/accepter_queues.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "coro/value_awaitable.hpp"
#include "device.hpp"
#include "memory_map.hpp"

#include <doca_eth_txq.h>
#include <doca_eth_txq_cpu_data_path.h>
#include <doca_flow.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>

/**
 * DOCA Ethernet functionality for sending of raw ethernet frames
 * 
 * See https://docs.nvidia.com/doca/sdk/doca+ethernet/index.html
 */
namespace shoc {
    /**
     * eth_txq configuration options, mirroring DOCA Ethernet
     */
    struct eth_txq_config {
        std::uint32_t max_burst_size;
        std::uint32_t max_send_buf_list_len = 1;
        std::uint8_t metadata_num = 0;
        std::uint16_t mss = 1500;
        std::uint16_t max_lso_header_size = 74;
        doca_eth_txq_type type = DOCA_ETH_TXQ_TYPE_REGULAR;
        bool l3_chksum_offload = false;
        bool l4_chksum_offload = false;
        bool wait_on_time_offload = false;
    };

    /**
     * Transceiver queue for raw ethernet frames
     */
    class eth_txq:
        public context<
            doca_eth_txq,
            doca_eth_txq_destroy,
            doca_eth_txq_as_doca_ctx
        >
    {
    public:
        eth_txq(
            context_parent *parent,
            device dev,
            std::uint32_t max_tasks,
            eth_txq_config &cfg
        );

        /**
         * Send a raw ethernet frame
         *
         * @param pkt buffer that contains the raw eth frame in its data region
         */
        auto send(buffer &pkt) -> coro::status_awaitable<>;

        /**
         * Send a payload with LSO, i.e. let the hardware segment it into a number of segments/packets/frames.
         *
         * Headers are supplied separately from the payload in a DOCA gather list, which as far as I can tell
         * is supposed to allow one to handle frame headers (eth), packet headers (ip4/6), and segment headers
         * (tcp) separately.
         *
         * @param payload buffer that contains the data to be segmented and sent
         * @param headers DOCA gather list containing the headers that should be prepended to each frame.
         */
        auto lso_send(buffer &payload, doca_gather_list *headers) -> coro::status_awaitable<>;

    private:
        device dev_;
    };
}
