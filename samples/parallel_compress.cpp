#include "shoc/buffer_inventory.hpp"
#include "shoc/compress.hpp"
#include "shoc/logger.hpp"
#include "shoc/memory_map.hpp"
#include "shoc/progress_engine.hpp"

#include <boost/cobalt.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <doca_log.h>

struct cache_aligned_memory {
    std::vector<char> storage;
    std::span<char> data;

    cache_aligned_memory(std::size_t size):
        storage(size + 64)
    {
        auto *base = static_cast<void*>(storage.data());
        auto space = storage.size();
        std::align(64, size, base, space);
        data = std::span { static_cast<char*>(base), size };
    }
};

auto compress_file(
    shoc::progress_engine_lease engine,
    std::istream &in,
    std::ostream &out
) -> boost::cobalt::detached {
    std::uint32_t batches;
    std::uint32_t batchsize;
    std::uint32_t const parallelism = 4;

    in.read(reinterpret_cast<char *>(&batches), sizeof batches);
    in.read(reinterpret_cast<char *>(&batchsize), sizeof batchsize);

    shoc::logger->info("compressing {} batches of size {}", batches, batchsize);

    auto filesize = batches * batchsize;
    auto src_mem = cache_aligned_memory(filesize);
    auto dst_mem = cache_aligned_memory(filesize);
    auto src_data = src_mem.data;
    auto dst_data = dst_mem.data;

    in.read(src_data.data(), filesize);

    auto dev = shoc::device::find(shoc::device_capability::compress_deflate);
    auto mmap_src = shoc::memory_map { dev, src_data };
    auto mmap_dst = shoc::memory_map { dev, dst_data };
    auto buf_inv = shoc::buffer_inventory { batches * 2 };

    auto src_buffers = std::vector<shoc::buffer>{};
    auto dst_buffers = std::vector<shoc::buffer>{};

    src_buffers.reserve(batches);
    dst_buffers.reserve(batches);

    for(auto i : std::ranges::views::iota(0u, batches)) {
        auto offset = i * batchsize;

        src_buffers.push_back(buf_inv.buf_get_by_data(mmap_src, src_data.data() + offset, batchsize));
        dst_buffers.push_back(buf_inv.buf_get_by_addr(mmap_dst, dst_data.data() + offset, batchsize));
    }

    auto compress = co_await engine->create_context<shoc::compress_context>(dev, parallelism);

    auto start = std::chrono::steady_clock::now();

    std::array<shoc::compress_awaitable, parallelism> waiters;

    for(auto i : std::ranges::views::iota(0u, batches)) {
        auto waiter_index = i % parallelism;
        auto &waiter = waiters[waiter_index];

        if(i >= parallelism) {
            shoc::logger->info("waiting for chunk {}", i - parallelism);
            co_await waiter;
        }

        waiter = compress->compress(src_buffers[i], dst_buffers[i]);
    }

    for(auto &waiter: waiters) {
        shoc::logger->info("waiting for final chunks...");
        co_await waiter;
    }

    auto end = std::chrono::steady_clock::now();

    co_await compress->stop();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto data_rate = filesize * 1e9 / elapsed_ns.count() / (1 << 30);

    auto json = nlohmann::json{};
    json["elapsed_us"] = elapsed_ns.count() / 1e3;
    json["data_rate_gibps"] = data_rate;

    std::cout << json.dump(4) << std::endl;

    if(out) {
        out.write(reinterpret_cast<char const *>(&batches), sizeof batches);
        out.write(reinterpret_cast<char const *>(&batchsize), sizeof batchsize);

        for(auto &buf : dst_buffers) {
            auto data = buf.data();
            std::uint32_t size = data.size();

            out.write(reinterpret_cast<char const*>(&size), sizeof size);
            out.write(data.data(), data.size());
        }
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main try {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    shoc::logger->set_level(spdlog::level::warn);

    if(argc < 2) {
        std::cerr << "Usage: " << argv[0] << " INFILE [OUTFILE]\n";
        co_return -1;
    }

    auto in  = std::ifstream(argv[1], std::ios::binary);
    auto out = argc < 2 ? std::ofstream{} : std::ofstream(argv[2], std::ios::binary);

    auto engine = shoc::progress_engine{};

    compress_file(&engine, in, out);

    co_await engine.run();
} catch(shoc::doca_exception &ex) {
    shoc::logger->error("ecode = {}, message = {}", static_cast<int>(ex.doca_error()), ex.what());
}
