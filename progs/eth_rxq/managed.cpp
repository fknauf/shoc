#include "../env.hpp"

#include <shoc/shoc.hpp>
#include <boost/asio.hpp>
#include <boost/cobalt.hpp>
#include <cppcodec/hex_lower.hpp>

#include <cstdint>
#include <iostream>
#include <vector>

using timer = boost::cobalt::use_op_t::as_default_on_t<boost::asio::steady_timer>;

auto handle_packets(
    [[maybe_unused]] shoc::progress_engine_lease engine,
    shoc::shared_scoped_context<shoc::eth_rxq_managed> rss
) -> boost::cobalt::promise<void> try {
    for(;;) {
        auto buf = co_await rss->receive();

        std::cout << cppcodec::hex_lower::encode(buf.data()) << '\n';
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->info("stopped handling packets: {}", e.what());
}

auto do_network_stuff(
    shoc::progress_engine_lease engine,
    char const *ibdev_name
) -> boost::cobalt::detached {
    auto flow_lib = shoc::flow::library_scope::config{}
        .set_pipe_queues(1)
        .set_mode_args("vnf,isolated")
        .set_nr_counters(1 << 19)
        .build();

    shoc::logger->info("Flow-Lib initialized, starting RSS...");

    auto dev = shoc::device::find_by_ibdev_name(ibdev_name, shoc::device_capability::eth_rxq_cpu_managed_mempool);

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

    auto packet_memory = shoc::aligned_memory { 1 << 28 };
    auto packet_mmap = shoc::memory_map { dev, packet_memory.as_writable_bytes(), DOCA_ACCESS_FLAG_LOCAL_READ_WRITE };
    auto packet_buffer = shoc::eth_rxq_packet_buffer { packet_mmap, 0, static_cast<std::uint32_t>(packet_memory.as_bytes().size()) };

    auto rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, cfg, packet_buffer);

    shoc::logger->info("RSS started, creating ingress port...");

    auto ingress = shoc::flow::port::config{}
        .set_port_id(0)
        .set_operation_state(DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE)
        .set_actions_mem_size(4096)
        .build();

    shoc::logger->info("ingress port created, setting up filter pipe...");

    doca_flow_match match = {};
    match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
    match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = 0xffffffff;
    match.outer.ip4.dst_ip = 0xffffffff;
    match.outer.tcp.l4_port.src_port = 0xffff;
    match.outer.tcp.l4_port.dst_port = 12345;

    doca_flow_actions actions = {};
    doca_flow_actions *actions_idx[] = { &actions };

    auto filter = shoc::flow::pipe::config { ingress }
        .set_name("ROOT")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_nr_entries(10)
        .set_domain(DOCA_FLOW_PIPE_DOMAIN_DEFAULT)
        .set_match(match)
        .set_actions(actions_idx)
        .build(rss->flow_target(), shoc::flow::fwd_kernel{});

    shoc::logger->info("Filter pipe created, will start handling packets now.");

    auto packet_coro = handle_packets(engine, rss);

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

    do_network_stuff(&engine, env.ibdev_name);

    co_await engine.run();
}
