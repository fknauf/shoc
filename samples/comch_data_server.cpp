#include "env.hpp"

#include <shoc/aligned_memory.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/producer.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/logger.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <boost/cobalt.hpp>

#include <iostream>
#include <memory>
#include <ranges>
#include <string_view>
#include <vector>

auto prepare_data(
    std::uint32_t block_count,
    std::uint32_t block_size
) {
    auto blocks = shoc::aligned_blocks(block_count, block_size);

    for(auto i : std::ranges::views::iota(std::uint32_t{}, block_count)) {
        std::ranges::fill(blocks.writable_block(i), static_cast<std::byte>(i));
    }

    return blocks;
}

auto send_blocks(
    shoc::comch::scoped_server_connection con,
    shoc::aligned_blocks &data,
    shoc::memory_map &mmap,
    shoc::buffer_inventory &bufinv
) -> boost::cobalt::detached try {
    auto prod = co_await con->create_producer(16);
    auto send_status = co_await con->send(fmt::format("{} {}", data.block_count(), data.block_size()));

    if(send_status != DOCA_SUCCESS) {
        shoc::logger->error("failed to send data gemoetry");
        co_return;
    }

    shoc::logger->debug("sent geometry: {} x {}", data.block_count(), data.block_size());

    auto remote_consumer = co_await con->accept_consumer();

    for(auto i : std::ranges::views::iota(std::uint32_t{}, data.block_count())) {
        shoc::logger->debug("sending block {}", i);

        auto buffer = bufinv.buf_get_by_data(mmap, data.block(i));
        auto status = co_await prod->send(buffer, {}, remote_consumer);

        if(status != DOCA_SUCCESS) {
            shoc::logger->error("producer failed to send buffer: {}", doca_error_get_descr(status));
            co_return;
        }
    }
} catch(boost::asio::bad_executor &e) {
    shoc::logger->debug("{}", e.what());
}

auto serve(
    shoc::progress_engine_lease engine,
    shoc::pci_address dev_pci,
    char const *rep_pci
) -> boost::cobalt::detached {
    auto dev = shoc::device::find(dev_pci, shoc::device_capability::comch_server);
    auto rep = shoc::device_representor::find_by_pci_addr(dev, rep_pci, DOCA_DEVINFO_REP_FILTER_NET);
    auto data = prepare_data(256, 1 << 20);
    auto mmap = shoc::memory_map { dev, data.as_bytes(), DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = shoc::buffer_inventory { 32 };

    auto server = co_await shoc::comch::server::create(engine, "shoc-data-test", dev, rep);

    std::cout << "accepting connections." << std::endl;

    for(;;) {
        auto con = co_await server->accept();
        send_blocks(std::move(con), data, mmap, bufinv);
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    //shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    //shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_dpu{};
    auto engine = shoc::progress_engine{};
    
    serve(&engine, env.dev_pci, env.rep_pci);
    
    co_await engine.run();
}
