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

namespace shoc {
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

        auto send(buffer &pkt) -> coro::status_awaitable<>;
        auto lso_send(buffer &payload, doca_gather_list *headers) -> coro::status_awaitable<>;

    private:
        device dev_;
    };
}
