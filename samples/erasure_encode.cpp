#include <shoc/aligned_memory.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/device.hpp>
#include <shoc/erasure_coding.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <cppcodec/base64_rfc4648.hpp>
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include <boost/cobalt.hpp>

#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <span>
#include <string>

auto slurp_file(
    std::filesystem::path const &filename,
    std::uint32_t block_size
) -> shoc::aligned_blocks {
    auto filesize = file_size(filename);
    auto block_count = (filesize - 1) / block_size + 1;

    shoc::logger->debug("reading {} into {} blocks of {} bytes", filename.string(), block_count, block_size);

    auto data_blocks = shoc::aligned_blocks(block_count, block_size);
    auto data_bytes = data_blocks.as_writable_bytes();
    
    auto in = std::ifstream(filename.c_str());
    auto read_bytes = in.readsome(reinterpret_cast<char*>(data_bytes.data()), filesize);
    std::ranges::fill(data_bytes.subspan(read_bytes), std::byte{});

    return data_blocks;
}

auto dump_results(
    std::filesystem::path const &filename,
    shoc::aligned_blocks const &data_blocks,
    shoc::aligned_blocks const &rdnc_blocks
) -> void {
    using base64 = cppcodec::base64_rfc4648;

    auto json = nlohmann::json{
        { "data_blocks", data_blocks.block_count() },
        { "rdnc_blocks", rdnc_blocks.block_count() },
        { "block_size", data_blocks.block_size() },
        { "blocks", nlohmann::json::array() }
    };

    for(auto i : std::views::iota(std::size_t{0}, data_blocks.block_count())) {
        json["blocks"] += {
                { "type", "data" },
                { "index", i },
                { "content", base64::encode(data_blocks.block(i)) }
            };
    };

    for(auto i : std::views::iota(std::uint32_t{0}, rdnc_blocks.block_count())) {
        json["blocks"] += {
                { "type", "redundancy" },
                { "index", data_blocks.block_count() + i },
                { "content", base64::encode(rdnc_blocks.block(i)) }
            };
    };

    auto out = std::ofstream(filename);
    out << json.dump(4) << std::endl;
}

auto encode_fiber(
    shoc::progress_engine_lease engine,
    std::filesystem::path const &input_filename,
    std::filesystem::path const &output_filename,
    std::uint32_t blocksize,
    std::uint32_t rdnc_block_count
) -> boost::cobalt::detached try {
    auto dev = shoc::device::find(shoc::device_capability::erasure_coding);
    auto ctx = co_await shoc::ec_context::create(engine, dev);
    auto bufinv = shoc::buffer_inventory { 2 };

    auto data_blocks = slurp_file(input_filename, blocksize);
    auto data_bytes = data_blocks.as_writable_bytes();
    auto data_mmap = shoc::memory_map { dev, data_bytes };
    auto data_buf = bufinv.buf_get_by_data(data_mmap, data_bytes);

    auto rdnc_blocks = shoc::aligned_blocks(rdnc_block_count, blocksize);
    auto rdnc_bytes = rdnc_blocks.as_writable_bytes();
    auto rdnc_mmap = shoc::memory_map { dev, rdnc_bytes };
    auto rdnc_buf = bufinv.buf_get_by_addr(rdnc_mmap, rdnc_bytes);

    shoc::logger->debug("read input data: {} blocks of {} bytes", data_blocks.block_count(), data_blocks.block_size());
    shoc::logger->debug("creating {} redundancy blocks of {} bytes", rdnc_blocks.block_count(), rdnc_blocks.block_size());

    auto coding_matrix = ctx->coding_matrix(DOCA_EC_MATRIX_TYPE_CAUCHY, data_blocks.block_count(), rdnc_block_count);
    auto err = co_await ctx->create(coding_matrix, data_buf, rdnc_buf);
    if(err != DOCA_SUCCESS) {
        shoc::logger->error("unable to encode: {}", doca_error_get_descr(err));
        co_return;
    }

    dump_results(output_filename, data_blocks, rdnc_blocks);
} catch(shoc::doca_exception const &e) {
    shoc::logger->error("SHOC error: {}", e.what());
} catch(std::exception const &e) {
    shoc::logger->error("generic error: {}", e.what());
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main try {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto options = cxxopts::Options("shoc-erasure_encoder", "Erasure encoder demo");

    options.add_options()
        ("b,blocksize", "size of a data block in bytes", cxxopts::value<std::uint32_t>()->default_value("512"))
        ("r,redundancy", "number of redundancy blocks", cxxopts::value<std::uint32_t>()->default_value("4"))
        ("i,input", "input file", cxxopts::value<std::filesystem::path>())
        ("o,output", "output file (json)", cxxopts::value<std::filesystem::path>()->default_value("ec_blocks.json"))
        ;

    auto cmdline = options.parse(argc, argv);

    auto block_size = cmdline["blocksize"].as<std::uint32_t>();
    auto rdnc_blocks = cmdline["redundancy"].as<std::uint32_t>();
    auto input_file = cmdline["input"].as<std::filesystem::path>();
    auto output_file = cmdline["output"].as<std::filesystem::path>();

    auto engine = shoc::progress_engine{};

    encode_fiber(&engine, input_file, output_file, block_size, rdnc_blocks);

    co_await engine.run();
} catch(std::exception &e) {
    shoc::logger->error("error in co_main: {}", e.what());
}
