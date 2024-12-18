#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/comch/consumer.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/logger.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <iostream>
#include <sstream>
#include <string_view>

struct cache_aligned_storage {
    cache_aligned_storage(std::uint32_t block_count, std::uint32_t block_size):
        block_count { block_count },
        block_size { block_size },
        buffer(block_count * block_size + 64),
        blocks(block_count)
    {
        void *base_ptr = buffer.data();
        auto space = buffer.size();

        std::align(64, block_count * block_size, base_ptr, space);
        bytes = std::span { static_cast<std::byte*>(base_ptr), block_count * block_size };

        for(auto i : std::ranges::views::iota(std::uint32_t{}, block_count)) {
            blocks[i] = bytes.subspan(i * block_size, block_size);
        }
    }

    std::uint32_t block_count;
    std::uint32_t block_size;
    std::vector<std::byte> buffer;
    std::span<std::byte> bytes;
    std::vector<std::span<std::byte>> blocks;
};

auto receive_blocks(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("81:00.0", doca::device_capability::comch_client);

    auto client = co_await engine->create_context<doca::comch::client>("vss-data-test", dev);
    auto geometry_message = co_await client->msg_recv();

    std::uint32_t block_count, block_size;
    std::istringstream geometry_parser(geometry_message);
    geometry_parser >> block_count >> block_size;

    if(!geometry_parser) {
        doca::logger->error("could not parse geometry from message {}", geometry_message);
        co_return;
    }

    auto memory = cache_aligned_storage { block_count, block_size };
    auto mmap = doca::memory_map { dev, memory.bytes, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = doca::buffer_inventory { 1 };

    auto consumer = co_await client->create_consumer(mmap, 16);

    auto start = std::chrono::steady_clock::now();

    for(auto block : memory.blocks) {
        auto buffer = bufinv.buf_get_by_addr(mmap, block);
        auto result = co_await consumer->post_recv(buffer);

        if(result.status != DOCA_SUCCESS) {
            doca::logger->error("post_recv failed with error: {}", doca_error_get_descr(result.status));
            co_return;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "elapsed time: " << elapsed.count() << " us\n";
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    doca::logger->set_level(spdlog::level::warn);

    auto engine = doca::progress_engine{};

    receive_blocks(&engine);
    engine.main_loop();
}
