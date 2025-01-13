#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/comch/consumer.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/logger.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <nlohmann/json.hpp>

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

auto receive_blocks(
    shoc::progress_engine *engine,
    char const *pci_addr
) -> shoc::coro::fiber {
    auto dev = shoc::device::find_by_pci_addr(pci_addr, shoc::device_capability::comch_client);

    auto client = co_await engine->create_context<shoc::comch::client>("shoc-data-test", dev);
    auto geometry_message = co_await client->msg_recv();

    std::uint32_t block_count, block_size;
    std::istringstream geometry_parser(geometry_message);
    geometry_parser >> block_count >> block_size;

    if(!geometry_parser) {
        shoc::logger->error("could not parse geometry from message {}", geometry_message);
        co_return;
    }

    auto memory = cache_aligned_storage { block_count, block_size };
    auto mmap = shoc::memory_map { dev, memory.bytes, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = shoc::buffer_inventory { 1 };

    auto consumer = co_await client->create_consumer(mmap, 16);

    auto start = std::chrono::steady_clock::now();

    for(auto block : memory.blocks) {
        auto buffer = bufinv.buf_get_by_addr(mmap, block);
        auto result = co_await consumer->post_recv(buffer);

        if(result.status != DOCA_SUCCESS) {
            shoc::logger->error("post_recv failed with error: {}", doca_error_get_descr(result.status));
            co_return;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto data_rate = block_count * block_size * 1e9 / elapsed_ns.count() / (1 << 30);

    auto json = nlohmann::json{};

    json["elapsed_us"] = elapsed_ns.count() / 1e3;
    json["data_rate_gibps"] = data_rate;
    json["data_error"] = false;

    for(auto i : std::ranges::views::iota(std::uint32_t{}, memory.block_count)) {
        if(std::ranges::any_of(memory.blocks[i], [i](std::byte b) { return b != static_cast<std::byte>(i); })) {
            shoc::logger->error("Block {} contains unexpected data", i);
            json["data_error"] = true;
            break;
        }
    }

    std::cout << json.dump(4) << std::endl;
}

int main() {
    //shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    //shoc::logger->set_level(spdlog::level::warn);

    auto env_dev = std::getenv("DOCA_DEV_PCI");
    
    auto engine = shoc::progress_engine{};

    receive_blocks(
        &engine,
        env_dev ? env_dev : "81:00.0"
    );
    engine.main_loop();
}
