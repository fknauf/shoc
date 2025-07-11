#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/dma.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <spdlog/fmt/bin_to_hex.h>
#include <nlohmann/json.hpp>

#include <boost/cobalt.hpp>

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
            throw shoc::doca_exception(DOCA_ERROR_INVALID_VALUE);
        }

        auto result = data_extents {};

        std::memcpy(&result.block_count, msg.data(), 4);
        std::memcpy(&result.block_size, msg.data() + 4, 4);
        result.remote_desc_buffer.assign(msg.begin() + 8, msg.end());

        return result;
    }

    auto remote_desc() const -> shoc::memory_map::export_descriptor {
        return { .base_ptr = remote_desc_buffer.data(), .length = remote_desc_buffer.size() };
    }

    auto block_indices() const {
        return std::ranges::views::iota(std::uint32_t{}, block_count);
    }
};

auto dma_receive(
    shoc::progress_engine_lease engine,
    shoc::pci_address pci_addr,
    std::uint32_t parallelism
) -> boost::cobalt::detached {
    auto dev = shoc::device::find(
        pci_addr,
        shoc::device_capability::comch_client,
        shoc::device_capability::dma
    );

    auto client = co_await engine->create_context<shoc::comch::client>("dma-test", dev);
    auto extents_msg = co_await client->msg_recv();
    auto extents = data_extents::from_message(extents_msg);

    shoc::logger->debug("got extents {} x {}", extents.block_count, extents.block_size);

    auto local_mem = std::vector<std::byte>(extents.block_count * extents.block_size);
    auto local_mmap = shoc::memory_map { dev, local_mem, DOCA_ACCESS_FLAG_PCI_READ_WRITE };

    auto remote_mmap = shoc::memory_map { dev, extents.remote_desc() };
    auto remote_mem = remote_mmap.span();

    auto inv = shoc::buffer_inventory { 1024 };

    auto slots = std::min(parallelism, extents.block_count);
    auto dma = co_await engine->create_context<shoc::dma_context>(dev, slots + 1);
    auto pending = std::vector<shoc::coro::status_awaitable<>>(slots);

    auto start = std::chrono::steady_clock::now();

    // parallel offload in one loop: first slots iterations fill pending, after that a pending
    // task will be awaited before another one is pushed. The last few iterations only await
    // pending tasks and don't push new ones.
    for(auto i : std::ranges::views::iota(std::uint32_t{}, extents.block_count + slots)) {
        auto slot = i % slots;

        if(i > slots) {
            auto status = co_await pending[slot];

            if(status != DOCA_SUCCESS) {
                shoc::logger->error("dma memcpy failed: {}", doca_error_get_descr(status));
                co_return;
            }
        }

        if(i < extents.block_count) {
            auto offset = i * extents.block_size;
            auto local_block = std::span { local_mem.data() + offset, extents.block_size };
            auto local_buf = inv.buf_get_by_addr(local_mmap, local_block);
            auto remote_block = remote_mem.subspan(offset, extents.block_size);
            auto remote_buf = inv.buf_get_by_data(remote_mmap, remote_block);

            pending[slot] = dma->memcpy(remote_buf, local_buf);
        }
    }

    auto end = std::chrono::steady_clock::now();

    co_await client->send("done");

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto data_rate = local_mem.size() * 1e9 / elapsed_ns.count() / (1 << 30);

    auto json = nlohmann::json{};

    json["elapsed_us"] = elapsed_ns.count() / 1e3;
    json["data_rate_gibps"] = data_rate;
    json["data_error"] = false;

    auto skip_verify = getenv("SKIP_VERIFY");

    if(skip_verify == nullptr || std::string { skip_verify } != "1") {
        for(auto i : extents.block_indices()) {
            auto local_block = std::span { local_mem.data() + i * extents.block_size, extents.block_size };

            if(std::ranges::any_of(local_block, [i](std::byte b) { return b != static_cast<std::byte>(i); })) {
                json["data_error"] = true;
                break;
            }
        }
    }

    std::cout << json.dump(4) << std::endl;
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    //shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    //shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_host{};
    auto engine = shoc::progress_engine{ };

    std::uint32_t parallelism = argc < 2 ? 1 : std::atoi(argv[1]);

    dma_receive(&engine, env.dev_pci, parallelism);

    co_await engine.run();
}
