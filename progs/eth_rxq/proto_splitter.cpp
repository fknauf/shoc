#include "../env.hpp"

#include <shoc/shoc.hpp>
#include <boost/asio.hpp>
#include <boost/cobalt.hpp>
#include <cppcodec/hex_lower.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using timer = boost::cobalt::use_op_t::as_default_on_t<boost::asio::steady_timer>;

auto handle_packets(
    [[maybe_unused]] shoc::progress_engine_lease engine,
    shoc::shared_scoped_context<shoc::eth_rxq_managed> rss,
    char const *label
) -> boost::cobalt::promise<void> try {
    for(;;) {
        auto buf = co_await rss->receive();

        std::cout << label << ": " << cppcodec::hex_lower::encode(buf.data()) << '\n';
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->info("{} stopped handling packets: {}", label, e.what());
}

auto create_splitter_pipe(
    shoc::flow::port &ingress,
    shoc::flow::flow_fwd fwd_tcp,
    shoc::flow::flow_fwd fwd_udp,
    shoc::flow::flow_fwd fwd_icmp
) {
    auto splitter_pipe = shoc::flow::pipe::config { ingress }
        .set_name("SPLITTER_PIPE")
        .set_type(DOCA_FLOW_PIPE_CONTROL)
        .set_is_root(false)
        .build();

    doca_flow_match tcp_match = {};
    tcp_match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
    splitter_pipe.control_add_entry(0, 0, tcp_match, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, fwd_tcp);

    doca_flow_match udp_match = {};
    udp_match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
    splitter_pipe.control_add_entry(0, 0, udp_match, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, fwd_udp);

    doca_flow_match icmp_match = {};
    icmp_match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_ICMP;
    splitter_pipe.control_add_entry(0, 0, icmp_match, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, fwd_icmp);

    return splitter_pipe;
}

// we need a dedicated root pipe because DOCA Flow doesn't allow parser_meta matching on control pipes.
// At least that's what I think; it's not documented, and the error messages are quite cryptic.
auto create_root_pipe(
    shoc::flow::port &ingress,
    shoc::flow::flow_fwd fwd
) {
    doca_flow_match all_match = {};

    auto root_pipe = shoc::flow::pipe::config { ingress }
        .set_name("ROOT_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_match(all_match)
        .build(fwd, shoc::flow::fwd_drop{});

    root_pipe.add_entry(0, all_match, std::nullopt, std::nullopt, std::monostate{}, 0, nullptr);

    return root_pipe;
}

auto split_packets_on_l4_protocol(
    shoc::progress_engine_lease engine,
    shoc::ibdev_name ibdev_name
) -> boost::cobalt::detached {
    using namespace std::literals::chrono_literals;

    auto dev = shoc::device::find(ibdev_name, shoc::device_capability::eth_rxq_cpu_managed_mempool);

    auto flow_lib = shoc::flow::library_scope::config{}
        .set_pipe_queues(1)
        .set_mode_args("vnf,hws,isolated")
        .set_nr_counters(1 << 19)
        .build();

    shoc::logger->info("Flow-Lib initialized, setting up ingress port...");

    auto ingress = shoc::flow::port::config{}
        .set_port_id(0)
        .set_dev(dev)
        .build();

    shoc::logger->info("ingress port created, setting up RSS...");

    auto cfg = shoc::eth_rxq_config {
        .max_burst_size = 256,
        .max_packet_size = 1600,
        .metadata_num = 1,
        .enable_flow_tag = true,
        .enable_rx_hash = true,
        .packet_headroom = 0,
        .packet_tailroom = 0,
        .enable_timestamp = false
    };

    auto tcp_packet_memory = shoc::eth_rxq_packet_memory { 1 << 28, dev };
    auto tcp_rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, cfg, tcp_packet_memory.as_buffer());
    auto udp_packet_memory = shoc::eth_rxq_packet_memory { 1 << 28, dev };
    auto udp_rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, cfg, udp_packet_memory.as_buffer());
    auto icmp_packet_memory = shoc::eth_rxq_packet_memory { 1 << 28, dev };
    auto icmp_rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, cfg, icmp_packet_memory.as_buffer());

    shoc::logger->info("RSS started, creating pipe...");    

    auto splitter = create_splitter_pipe(ingress, tcp_rss->flow_target(), udp_rss->flow_target(), icmp_rss->flow_target());
    auto root_pipe = create_root_pipe(ingress, splitter);

    ingress.process_entries(0, 10ms, 4);

    shoc::logger->info("Entries processed, will start handling packets now.");

    auto tcp_coro = handle_packets(engine, tcp_rss, "TCP");
    auto udp_coro = handle_packets(engine, udp_rss, "UDP");
    auto icmp_coro = handle_packets(engine, icmp_rss, "ICMP");

    auto tim = timer { co_await boost::cobalt::this_coro::executor };
    tim.expires_after(std::chrono::seconds(30));
    co_await tim.async_wait();

    co_await tcp_rss->stop();
    co_await udp_rss->stop();
    co_await icmp_rss->stop();
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto engine = shoc::progress_engine{};
    auto env = bluefield_env{};

    split_packets_on_l4_protocol(&engine, env.ibdev_name);

    co_await engine.run();
}
