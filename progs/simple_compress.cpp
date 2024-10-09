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
#include <ranges>

#include <fmt/core.h>

#include <doca_log.h>

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
    std::vector<char> src_data(filesize);
    std::vector<char> dst_data(filesize);
    std::vector<std::span<char>> dst_ranges(batches);

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
        auto result = co_await compress->compress(src, dst);
        doca::logger->debug("compress_chunk complete: {}, crc = {}, adler = {}", i, result.crc_cs(), result.adler_cs());

        dst_ranges[i] = dst.data();
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    doca::logger->info("elapsed time: {} us", elapsed.count());

    co_await compress->stop();

    for(auto &data : dst_ranges) {
        std::uint32_t size = data.size();

        out.write(reinterpret_cast<char const*>(&size), sizeof size);
        out.write(data.data(), data.size());
    }

    co_return;
}

auto main(int argc, char *argv[]) -> int try {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    doca::logger->set_level(spdlog::level::trace);

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
