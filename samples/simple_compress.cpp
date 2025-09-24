#include <shoc/aligned_memory.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/compress.hpp>
#include <shoc/logger.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <boost/cobalt.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ranges>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <doca_log.h>

auto compress_file(
    shoc::progress_engine_lease engine,
    std::istream &in,
    std::ostream &out
) -> boost::cobalt::detached
{
    std::uint32_t batches;
    std::uint32_t batchsize;

    in.read(reinterpret_cast<char *>(&batches), sizeof batches);
    in.read(reinterpret_cast<char *>(&batchsize), sizeof batchsize);

    shoc::logger->debug("compressing {} batches of size {}", batches, batchsize);

    auto filesize = batches * batchsize;
    auto src_blocks = shoc::aligned_blocks(batches, batchsize);
    auto dst_blocks = shoc::aligned_blocks(batches, batchsize);
    auto src_data = src_blocks.as_writable_bytes();
    auto dst_data = dst_blocks.as_writable_bytes();
    auto dst_ranges = std::vector<std::span<char>>(batches);

    in.read(reinterpret_cast<char*>(src_data.data()), src_data.size());

    auto dev = shoc::device::find(shoc::device_capability::compress_deflate);
    auto mmap_src = shoc::memory_map { dev, src_data };
    auto mmap_dst = shoc::memory_map { dev, dst_data };
    auto buf_inv = shoc::buffer_inventory { 2 };

    auto compress = co_await shoc::compress_context::create(engine, dev, 16);

    auto start = std::chrono::steady_clock::now();

    for(auto i : std::ranges::views::iota(0u, batches)) {
        auto src = buf_inv.buf_get_by_data(mmap_src, src_blocks.block(i));
        auto dst = buf_inv.buf_get_by_addr(mmap_dst, dst_blocks.block(i));

        shoc::logger->debug("compressing chunk {}...", i);

        auto checksums = shoc::compress_checksums{};
        auto status = co_await compress->compress(src, dst, &checksums);

        shoc::logger->debug("compress_chunk complete: {}, status = {}, crc = {}, adler = {}",
                            i, static_cast<int>(status), checksums.crc, checksums.adler);

        dst_ranges[i] = dst.data();
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

        for(auto &data : dst_ranges) {
            std::uint32_t size = data.size();

            out.write(reinterpret_cast<char const*>(&size), sizeof size);
            out.write(data.data(), data.size());
        }

        co_return;
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

    auto in = std::ifstream(argv[1], std::ios::binary);
    auto out = argc < 3 ? std::ofstream{} : std::ofstream(argv[2], std::ios::binary);
    auto engine = shoc::progress_engine{};

    compress_file(&engine, in, out);

    co_await engine.run();
} catch(shoc::doca_exception &ex) {
    shoc::logger->error("ecode = {}, message = {}", static_cast<int>(ex.doca_error()), ex.what());
}