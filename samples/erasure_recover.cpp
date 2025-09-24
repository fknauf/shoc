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
#include <unordered_set>

class erasure_coded_data {
public:
    erasure_coded_data(
        std::filesystem::path const &filename,
        std::vector<std::uint32_t> ignored
    ) {
        using base64 = cppcodec::base64_rfc4648;

        auto in = std::ifstream{ filename.c_str() };
        auto json = nlohmann::json::parse(in);

        block_size_ = json["block_size"].get<std::uint32_t>();
        data_block_count_ = json["data_blocks"].get<std::uint32_t>();
        rdnc_block_count_ = json["rdnc_blocks"].get<std::uint32_t>();

        auto ignored_set = std::unordered_set<std::uint32_t>(ignored.begin(), ignored.end());
        assert(ignored.size() == ignored_set.size());

        available_blocks_ = shoc::aligned_blocks { data_block_count_, block_size_ };
        auto dest_idx = std::size_t{0};

        shoc::logger->debug("loading {} available blocks of {} bytes", data_block_count_, block_size_);

        for(auto block : json["blocks"]) {
            auto src_idx = block["index"].get<std::uint32_t>();

            if(ignored_set.contains(src_idx)) {
                continue;
            }

            auto content = base64::decode(block["content"].get<std::string_view>());
            assert(content.size() == block_size_);

            std::ranges::transform(
                content,
                available_blocks_.writable_block(dest_idx).begin(),
                [](std::uint8_t c) { return static_cast<std::byte>(c); }
            );
            ++dest_idx;

            if(dest_idx >= data_block_count_) {
                break;
            }
        }
    }

    [[nodiscard]] auto block_size() const { return block_size_; }
    [[nodiscard]] auto data_block_count() const { return data_block_count_; }
    [[nodiscard]] auto rdnc_block_count() const { return rdnc_block_count_; }
    [[nodiscard]] auto &available_blocks() const { return available_blocks_; }

private:
    std::uint32_t block_size_;
    std::uint32_t data_block_count_;
    std::uint32_t rdnc_block_count_;
    shoc::aligned_blocks available_blocks_;
    std::vector<std::uint32_t> block_indices_;
};

auto dump_results(
    std::filesystem::path const &filename,
    erasure_coded_data const &ec_data,
    shoc::aligned_blocks const &recovered_blocks,
    std::vector<std::uint32_t> const &ignored
) {
    auto ignored_set = std::unordered_set<std::uint32_t>(ignored.begin(), ignored.end());
    auto recovered_ix = std::uint32_t{0};
    auto available_ix = std::uint32_t{0};

    auto out = std::ofstream { filename.c_str() };
    auto dump = [&out](std::span<std::byte const> data) {
        auto data_baseptr = reinterpret_cast<char const*>(data.data());
        out.write(data_baseptr, data.size());
    };

    for(auto i : std::views::iota(std::uint32_t{0}, ec_data.data_block_count())) {
        if(ignored_set.contains(i)) {
            dump(recovered_blocks.block(recovered_ix));
            ++recovered_ix;
        } else {
            dump(ec_data.available_blocks().block(available_ix));
            ++available_ix;
        }
    }
}

auto recovery_fiber(
    shoc::progress_engine_lease engine,
    std::filesystem::path const &input_filename,
    std::filesystem::path const &output_filename,
    std::vector<std::uint32_t> const &ignored
) -> boost::cobalt::detached try {
    auto dev = shoc::device::find(shoc::device_capability::erasure_coding);
    auto ctx = co_await shoc::ec_context::create(engine, dev);
    auto bufinv = shoc::buffer_inventory { 2 };

    auto ec_data = erasure_coded_data { input_filename, ignored };

    auto &available_blocks = ec_data.available_blocks();
    auto available_bytes = available_blocks.as_bytes();
    auto available_mmap = shoc::memory_map { dev, available_bytes };
    auto available_buf = bufinv.buf_get_by_data(available_mmap, available_bytes);

    auto recovered_blocks = shoc::aligned_blocks(ignored.size(), available_blocks.block_size());
    auto recovered_bytes = recovered_blocks.as_writable_bytes();
    auto recovered_mmap = shoc::memory_map { dev, recovered_bytes };
    auto recovered_buf = bufinv.buf_get_by_addr(recovered_mmap, recovered_bytes);

    auto coding_matrix = ctx->coding_matrix(DOCA_EC_MATRIX_TYPE_CAUCHY, ec_data.data_block_count(), ec_data.rdnc_block_count());
    auto recover_matrix = ctx->recover_matrix(coding_matrix, ignored);
    auto err = co_await ctx->recover(recover_matrix, available_buf, recovered_buf);
    if(err != DOCA_SUCCESS) {
        shoc::logger->error("unable to recover: {}", doca_error_get_descr(err));
        co_return;
    }

    dump_results(output_filename, ec_data, recovered_blocks, ignored);
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
        ("i,input", "input file (json)", cxxopts::value<std::filesystem::path>()->default_value("ec_blocks.json"))
        ("o,output", "output file", cxxopts::value<std::filesystem::path>()->default_value("recovered.dat"))
        ("n,ignore", "indices to ignore (to pretend they're missing')", cxxopts::value<std::vector<std::uint32_t>>()->default_value("0,1"));
        ;

    auto cmdline = options.parse(argc, argv);

    auto input_file = cmdline["input"].as<std::filesystem::path>();
    auto output_file = cmdline["output"].as<std::filesystem::path>();
    auto ignored = cmdline["ignore"].as<std::vector<std::uint32_t>>();

    {
        std::ranges::sort(ignored);
        auto [ last, end ] = std::ranges::unique(ignored);
        ignored.erase(last, end);
    }

    auto engine = shoc::progress_engine{};

    recovery_fiber(&engine, input_file, output_file, ignored);

    co_await engine.run();
} catch(std::exception &e) {
    shoc::logger->error("error in co_main: {}", e.what());
}
