#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <spdlog/fmt/bin_to_hex.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

struct test_data {
    std::uint32_t block_count;
    std::uint32_t block_size;
    std::vector<std::byte> buffer;
    std::span<std::byte> bytes;
    std::vector<std::span<std::byte>> blocks;

    auto block(std::uint32_t i) const {
        return bytes.subspan(i * block_size, block_size);
    }

    test_data(
        std::uint32_t block_count,
        std::uint32_t block_size
    ):
        block_count { block_count },
        block_size { block_size },
        buffer(block_count * block_size + 64),
        blocks(block_count)
    {
        auto base_ptr = static_cast<void*>(buffer.data());
        auto space = buffer.size();
        std::align(64, block_size * block_count, base_ptr, space);

        bytes = std::span { static_cast<std::byte*>(base_ptr), block_size * block_count };

        for(auto i : std::ranges::views::iota(std::uint32_t{}, block_count)) {
            blocks[i] = block(i);
            std::ranges::fill(blocks[i], static_cast<std::byte>(i));
        }
    }
};

auto handle_connection(
    doca::progress_engine *engine,
    doca::device &dev,
    test_data const &data,
    doca::comch::scoped_server_connection conn
) -> doca::coro::fiber {
    auto dma = co_await engine->create_context<doca::dma_context>(dev, 16);
    auto msg = co_await conn->msg_recv();

    auto remote_desc = remote_buffer_descriptor { msg };

    auto remote_mmap = doca::memory_map { dev, remote_desc.export_desc };
    auto inv = doca::buffer_inventory { 2 };
    auto remote_buf = inv.buf_get_by_data(remote_mmap, remote_desc.src_range);

    auto local_mmap = doca::memory_map { dev, data.bytes };

    auto start = std::chrono::steady_clock::now();

    for(auto b : data.blocks) {
        auto local_buf = inv.buf_get_by_addr(local_mmap, b);
        auto status = co_await dma->memcpy(remote_buf, local_buf);

        if(status != DOCA_SUCCESS) {
            doca::logger->error("dma memcpy failed: {}", doca_error_get_descr(status));
            break;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "elapsed time: " << elapsed.count() << " us\n";
    std::cout << "data rate: " << data.bytes.size() * 1e6 / elapsed.count() / (1 << 30) << " GiB/s\n";

    [[maybe_unused]] auto ok_status = co_await conn->send("done");
}

auto dma_serve(
    doca::progress_engine *engine
) -> doca::coro::fiber {
    auto data = test_data { 256, 1 << 20 };
    auto dev = doca::device::find_by_pci_addr("03:00.0", { doca::device_capability::dma, doca::device_capability::comch_server });
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("dma-test", dev, rep);

    for(;;) {
        auto conn = co_await server->accept();
        handle_connection(engine, dev, data, std::move(conn));
    }
}

auto main() -> int {
    // doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    // doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    dma_serve(&engine);

    engine.main_loop();
}