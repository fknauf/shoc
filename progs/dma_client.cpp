#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <spdlog/fmt/bin_to_hex.h>

#include <iostream>
#include <ranges>
#include <span>
#include <sstream>
#include <string_view>
#include <vector>


auto dma_receive(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr(
        "81:00.0",
        {
            doca::device_capability::comch_client,
            doca::device_capability::dma
        }
    );

    auto dma = co_await engine->create_context<doca::dma_context>(dev, 1);
    auto client = co_await engine->create_context<doca::comch::client>("dma-test", dev);
    auto extents_msg = co_await client->msg_recv();

    auto block_count = std::uint32_t{};
    auto block_size = std::uint32_t{};
    auto remote_desc = doca::memory_map::export_descriptor{};
    auto extents_parser = std::istringstream(extents_msg);

    extents_parser >> block_count >> block_size >> remote_desc;
    if(!extents_parser) {
        doca::logger->error("unable to parse extents from message: {}", extents_msg);
        co_return;
    }

    std::cout << "got extents " << block_count << " x " << block_size << ", remote descr " << remote_desc.encode() << std::endl;

    auto local_mem = std::vector<std::byte>(block_count * block_size);
    auto local_mmap = doca::memory_map { dev, local_mem };
    auto remote_mmap = doca::memory_map { dev, remote_desc };
    auto remote_mem = remote_mmap.span();
    auto inv = doca::buffer_inventory { 2 };

    auto start = std::chrono::steady_clock::now();

    for(auto i : std::ranges::views::iota(std::uint32_t{}, block_count)) {
        auto offset = i * block_size;
        auto local_block = std::span { local_mem.data() + offset, block_size };
        auto local_buf = inv.buf_get_by_addr(local_mmap, local_block);
        auto remote_block = remote_mem.subspan(offset, block_size);
        auto remote_buf = inv.buf_get_by_data(remote_mmap, remote_block);

        auto status = co_await dma->memcpy(local_buf, remote_buf);

        if(status != DOCA_SUCCESS) {
            doca::logger->error("dma memcpy failed: {}", doca_error_get_descr(status));
            co_return;
        } else {
            std::cout << "got block " << i << std::endl;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "elapsed time: " << elapsed.count() << " us\n";
    std::cout << "data rate: " << local_mem.size() * 1e6 / elapsed.count() / (1 << 30) << " GiB/s\n";

    co_await client->send("done");
}

auto main() -> int {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    dma_receive(&engine);

    engine.main_loop();
}
