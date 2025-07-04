#include "../env.hpp"
#include <shoc/shoc.hpp>
#include <boost/cobalt.hpp>

#include <cxxopts.hpp>
#include <cppcodec/hex_lower.hpp>

#include <string>

auto send_packet(
    shoc::progress_engine_lease engine,
    std::vector<std::uint8_t> packet,
    std::string device_name,
    bool calculate_checksums
) -> boost::cobalt::detached {
    auto dev = shoc::device::find_by_ibdev_name(
        device_name, 
        {
            shoc::device_capability::eth_txq_cpu_regular,
            shoc::device_capability::eth_txq_l3_chksum_offload,
            shoc::device_capability::eth_txq_l4_chksum_offload
        }
    );

    auto txq_cfg = shoc::eth_txq_config {
        .max_burst_size = 256,
        .l3_chksum_offload = calculate_checksums,
        .l4_chksum_offload = calculate_checksums
    };

    auto mmap = shoc::memory_map { dev, packet, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto buf = bufinv.buf_get_by_data(mmap, packet);

    auto txq = co_await engine->create_context<shoc::eth_txq>(dev, 16, txq_cfg);
    auto status = co_await txq->send(buf);

    shoc::logger->info("Packet sent, status = {}", doca_error_get_descr(status));

    co_await engine->yield();
}

auto co_main(
    int argc,
    char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env{};
    auto options = cxxopts::Options("shoc-send-raw-packet", "Raw packet sender utility");

    options.add_options()
        ("p,packet", "packet string (hex)", cxxopts::value<std::string>()->default_value("1070fdb3513f02d1cf111051080045000020f29840004011fdd3c0a864dac0a864358dff3039000c21c3666f6f0a0000000000000000000000000000"))
        ("d,device", "device (ibdev name)", cxxopts::value<std::string>()->default_value(env.ibdev_name))
        ("c,calculate-checksums", "calculate L3 and L4 checksums?", cxxopts::value<bool>()->default_value("false"));
        ;

    auto cmdline = options.parse(argc, argv);

    auto packet = cppcodec::hex_lower::decode<std::vector<std::uint8_t>>(cmdline["packet"].as<std::string>());
    auto device_name = cmdline["device"].as<std::string>();
    auto calc_checksums = cmdline["calculate-checksums"].as<bool>();

    auto engine = shoc::progress_engine{};
    
    send_packet(
        &engine,
        std::move(packet),
        std::move(device_name),
        calc_checksums
    );

    co_await engine.run();
}
