#include "doca/buffer_inventory.hpp"
#include "doca/compress.hpp"
#include "doca/coro/fiber.hpp"
#include "doca/logger.hpp"
#include "doca/memory_map.hpp"
#include "doca/progress_engine.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ranges>

#include <fmt/core.h>

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
    doca::progress_engine *engine,
    std::istream &in,
    std::ostream &out
) -> doca::coro::fiber
{
    std::uint32_t batches;
    std::uint32_t batchsize;

    in.read(reinterpret_cast<char *>(&batches), sizeof batches);
    in.read(reinterpret_cast<char *>(&batchsize), sizeof batchsize);

    doca::logger->info("compressing {} batches of size {}", batches, batchsize);

    auto filesize = batches * batchsize;
    auto src_mem = cache_aligned_memory(filesize + 64);
    auto dst_mem = cache_aligned_memory(filesize + 64);
    auto src_data = src_mem.data;
    auto dst_data = dst_mem.data;
    auto dst_ranges = std::vector<std::span<char>>(batches);

    in.read(src_data.data(), filesize);

    out.write(reinterpret_cast<char const *>(&batches), sizeof batches);
    out.write(reinterpret_cast<char const *>(&batchsize), sizeof batchsize);

    auto dev = doca::device::find_by_capabilities(doca::device_capability::compress_deflate);
    auto mmap_src = doca::memory_map { dev, src_data };
    auto mmap_dst = doca::memory_map { dev, dst_data };
    auto buf_inv = doca::buffer_inventory { 2 };

    doca::logger->debug("engine = {}", static_cast<void*>(engine));

    auto compress = co_await engine->create_context<doca::compress_context>(dev, 16);

    auto start = std::chrono::steady_clock::now();

    for(auto i : std::ranges::views::iota(0u, batches)) {
        auto offset = i * batchsize;
        auto src = buf_inv.buf_get_by_data(mmap_src, src_data.data() + offset, batchsize);
        auto dst = buf_inv.buf_get_by_addr(mmap_dst, dst_data.data() + offset, batchsize);

        doca::logger->debug("compressing chunk {}...", i);

        auto checksums = doca::compress_checksums{};
        auto status = co_await compress->compress(src, dst, &checksums);

        doca::logger->debug("compress_chunk complete: {}, status = {}, crc = {}, adler = {}",
                            i, status, checksums.crc, checksums.adler);

        dst_ranges[i] = dst.data();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "elapsed time: " << elapsed.count() << "us\n";
    std::cout << "data rate: " << filesize / elapsed.count() * 1e6 / (1 << 30) << " GiB/s\n";

    co_await compress->stop();

    for(auto &data : dst_ranges) {
        std::uint32_t size = data.size();

        out.write(reinterpret_cast<char const*>(&size), sizeof size);
        out.write(data.data(), data.size());
    }

    co_return;
}

auto main(int argc, char *argv[]) -> int try {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    doca::logger->set_level(spdlog::level::warn);

    if(argc < 3) {
        std::cerr << "Usage: " << argv[0] << " INFILE OUTFILE\n";
        return -1;
    }

    auto in  = std::ifstream(argv[1], std::ios::binary);
    auto out = std::ofstream(argv[2], std::ios::binary);
    auto engine = doca::progress_engine {};

    doca::logger->debug("starting compress_file");

    compress_file(&engine, in, out);

    doca::logger->debug("spawned coroutine, starting main loop");

    engine.main_loop();

    doca::logger->debug("main loop finished.");
} catch(doca::doca_exception &ex) {
    doca::logger->error("ecode = {}, message = {}", ex.doca_error(), ex.what());
}
