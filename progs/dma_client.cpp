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

struct data_extents {
    std::uint32_t block_count;
    std::uint32_t block_size;
    std::vector<char> remote_desc_buffer;

    static auto from_message(std::string const &msg) -> data_extents {
        if(msg.size() <= 8) {
            throw doca::doca_exception(DOCA_ERROR_INVALID_VALUE);
        }

        auto result = data_extents {};

        std::memcpy(&result.block_count, msg.data(), 4);
        std::memcpy(&result.block_size, msg.data() + 4, 4);
        result.remote_desc_buffer.assign(msg.begin() + 8, msg.end());

        return result;
    }

    auto remote_desc() const -> doca::memory_map::export_descriptor {
        return { .base_ptr = remote_desc_buffer.data(), .length = remote_desc_buffer.size() };
    }

    auto block_indices() const {
        return std::ranges::views::iota(std::uint32_t{}, block_count);
    }
};

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
    auto extents = data_extents::from_message(extents_msg);

    std::cout << "got extents " << extents.block_count << " x " << extents.block_size << std::endl;

    auto local_mem = std::vector<std::byte>(extents.block_count * extents.block_size);
    auto local_mmap = doca::memory_map { dev, local_mem, DOCA_ACCESS_FLAG_PCI_READ_WRITE };

    auto remote_mmap = doca::memory_map { dev, extents.remote_desc() };
    auto remote_mem = remote_mmap.span();

    auto inv = doca::buffer_inventory { 2 };

    auto start = std::chrono::steady_clock::now();

    for(auto i : extents.block_indices()) {
        auto offset = i * extents.block_size;
        auto local_block = std::span { local_mem.data() + offset, extents.block_size };
        auto local_buf = inv.buf_get_by_addr(local_mmap, local_block);
        auto remote_block = remote_mem.subspan(offset, extents.block_size);
        auto remote_buf = inv.buf_get_by_data(remote_mmap, remote_block);

        auto status = co_await dma->memcpy(remote_buf, local_buf);

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

    for(auto i : extents.block_indices()) {
        auto local_block = std::span { local_mem.data() + i * extents.block_size, extents.block_size };

        if(std::ranges::any_of(local_block, [i](std::byte b) { return b != static_cast<std::byte>(i); })) {
            doca::logger->error("Block {} contains unexpected data", i);
        }
    }

    std::cout << "data verified correct.\n";
}

auto main() -> int {
    //doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    //doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    dma_receive(&engine);

    engine.main_loop();
}
