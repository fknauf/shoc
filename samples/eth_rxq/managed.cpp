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
    shoc::ibdev_name ibdev_name
) -> boost::cobalt::detached {
    using namespace std::literals::chrono_literals;

    auto dev = shoc::device::find(ibdev_name, shoc::device_capability::eth_rxq_cpu_managed_mempool);

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

    auto rss = co_await engine->create_context<shoc::eth_rxq_managed>(dev, 0, cfg, packet_buffer);

    shoc::logger->info("RSS started, creating filter pipe...");

    doca_flow_match all_match = {};

    doca_flow_actions actions = {};
    actions.meta.pkt_meta = htobe32(1234);
    doca_flow_actions *actions_idx[] = { &actions };

    auto filter = shoc::flow::pipe::config { ingress }
        .set_name("ROOT_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_domain(DOCA_FLOW_PIPE_DOMAIN_DEFAULT)
        .set_match(all_match)
        .set_actions(actions_idx)
        .build(rss->flow_target(), shoc::flow::fwd_drop{});
    
    filter.add_entry(0, all_match, actions, std::nullopt, std::monostate{}, 0, nullptr);
    ingress.process_entries(0, 10ms, 4);

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
