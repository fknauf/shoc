#include "env.hpp"

#include <shoc/eth_frame.hpp>
#include <shoc/shoc.hpp>
#include <boost/asio.hpp>
#include <boost/cobalt.hpp>
#include <cppcodec/hex_lower.hpp>

#include <endian.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <vector>

using timer = boost::cobalt::use_op_t::as_default_on_t<boost::asio::steady_timer>;

inline auto be_ipv4_addr(
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d
) -> std::uint32_t {
    return htobe32((a << 24) | (b << 16) | (c << 8) | d);
}

auto handle_packets(
    [[maybe_unused]] shoc::progress_engine_lease engine,
    shoc::shared_scoped_context<shoc::eth_rxq_managed> rss,
    shoc::shared_scoped_context<shoc::eth_txq> txq
) -> boost::cobalt::promise<void> try {
    for(;;) {
        auto buf = co_await rss->receive();

        std::cout << cppcodec::hex_lower::encode(buf.data()) << '\n';

        auto frame = reinterpret_cast<shoc::eth_frame*>(buf.data().data());
        auto packet = frame->ipv4_payload();
        auto segment = packet->udp_payload();

        auto src_port = segment->source_port();
        auto dst_port = segment->destination_port();

        auto src_ip = packet->source_address();
        auto dst_ip = packet->destination_address();

        std::ranges::swap_ranges(
            frame->source_mac(),
            frame->destination_mac()
        );

        packet
            ->source_address(dst_ip)
            ->destination_address(src_ip)
            ->update_header_checksum();

        segment
            ->source_port(dst_port)
            ->destination_port(src_port)
            ->update_checksum(*packet);

        auto send_status = co_await txq->send(buf);

        shoc::logger->info(
            "sent response. status = {}, ip4 header chksum = {}, udp chksum = {}",
            doca_error_get_descr(send_status),
            packet->header_checksum(),
            segment->checksum()
        );
        std::cout << cppcodec::hex_lower::encode(buf.data()) << '\n';
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->info("stopped handling packets: {}", e.what());
}

auto partial_tap(
    shoc::progress_engine_lease engine,
    char const *ibdev_name
) -> boost::cobalt::detached {
    using namespace std::literals::chrono_literals;

    auto dev = shoc::device::find_by_ibdev_name(
        ibdev_name, 
        {
            shoc::device_capability::eth_rxq_cpu_managed_mempool,
            shoc::device_capability::eth_txq_cpu_regular,
            shoc::device_capability::eth_txq_l3_chksum_offload,
            shoc::device_capability::eth_txq_l4_chksum_offload
        }
    );

    auto flow_lib = shoc::flow::library_scope::config{}
        .set_pipe_queues(1)
        .set_mode_args("vnf,isolated")
        .set_nr_counters(1 << 19)
        .build();

    shoc::logger->info("Flow-Lib initialized, setting up ingress port...");

    auto ingress = shoc::flow::port::config{}
        .set_port_id(0)
        .set_dev(dev)
        .build();

    shoc::logger->info("ingress port created, setting up RSS...");

    auto rxq_cfg = shoc::eth_rxq_config {
        .max_burst_size = 256,
        .max_packet_size = 1600,
        .metadata_num = 1,
        .enable_flow_tag = true,
        .enable_rx_hash = true,
        .packet_headroom = 0,
        .packet_tailroom = 0,
        .enable_timestamp = false
    };

    auto packet_memory = shoc::aligned_memory { 1 << 28 };
    auto packet_mmap = shoc::memory_map { dev, packet_memory.as_writable_bytes(), DOCA_ACCESS_FLAG_LOCAL_READ_WRITE };
    auto packet_buffer = shoc::eth_rxq_packet_buffer { packet_mmap, 0, static_cast<std::uint32_t>(packet_memory.as_bytes().size()) };

    auto rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, rxq_cfg, packet_buffer);

    auto txq_cfg = shoc::eth_txq_config {
        .max_burst_size = 256,
        .l3_chksum_offload = true,
        .l4_chksum_offload = true
    };

    auto txq = co_await engine->create_context<shoc::eth_txq>(dev, 16, txq_cfg);

    shoc::logger->info("RSS started, creating filter pipe...");

    doca_flow_match rss_match = {};
    rss_match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
    rss_match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    rss_match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
    rss_match.outer.udp.l4_port.dst_port = 0xffff;

    auto filter_pipe = shoc::flow::pipe::config { ingress }
        .set_name("FILTER_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(false)
        .set_match(rss_match)
        .build(rss->flow_target(), shoc::flow::fwd_kernel{});
    
    doca_flow_match entry_rss_match = {};
    entry_rss_match.outer.udp.l4_port.dst_port = htobe16(12345);

    filter_pipe.add_entry(0, entry_rss_match, std::nullopt, std::nullopt, rss->flow_target(), 0);

    doca_flow_match all_match = {};

    auto root_pipe = shoc::flow::pipe::config { ingress }
        .set_name("ROOT_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_match(all_match)
        .build(filter_pipe, shoc::flow::fwd_drop{});

    root_pipe.add_entry(0, all_match, std::nullopt, std::nullopt, filter_pipe, 0);

    ingress.process_entries(0, 10ms, 4);

    shoc::logger->info("Filter pipe created, will start handling packets now.");

    auto packet_coro = handle_packets(engine, rss, txq);

    auto tim = timer { co_await boost::cobalt::this_coro::executor };
    tim.expires_after(std::chrono::seconds(30));
    co_await tim.async_wait();

    co_await rss->stop();
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto engine = shoc::progress_engine{};
    auto env = bluefield_env{};

    partial_tap(&engine, env.ibdev_name);

    co_await engine.run();
}
