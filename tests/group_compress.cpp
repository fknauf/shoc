#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/compress.hpp>
#include <shoc/device.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <gtest/gtest.h>

#include <boost/cobalt.hpp>

#include <ranges>
#include <string>
#include <vector>

#define CO_FAIL(message) do { *report = (message); co_return; } while(false)
#define CO_ASSERT(condition, message) do { if(!(condition)) { CO_FAIL(message); } } while(false)

#define CO_ASSERT_EQ(val1, val2, message) CO_ASSERT((val1) == (val2), message)
#define CO_ASSERT_NE(val1, val2, message) CO_ASSERT((val1) != (val2), message)
#define CO_ASSERT_LT(val1, val2, message) CO_ASSERT((val1) <  (val2), message)
#define CO_ASSERT_LE(val1, val2, message) CO_ASSERT((val1) <= (val2), message)
#define CO_ASSERT_GT(val1, val2, message) CO_ASSERT((val1) >  (val2), message)
#define CO_ASSERT_GE(val1, val2, message) CO_ASSERT((val1) >= (val2), message)

TEST(docapp_compress, single_shot) {
    auto report = std::string { "fiber not started" };

    auto fiber_fn = [](
        shoc::progress_engine *engine,
        std::string *report
    ) -> boost::cobalt::detached {
        try {
            *report = "";

            auto dev = shoc::device::find_by_capabilities(shoc::device_capability::compress_deflate);

            auto buf_inv = shoc::buffer_inventory { 3 };

            auto src_data = std::string { "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua." };
            auto src_mmap = shoc::memory_map { dev, src_data };
            auto src_buf = buf_inv.buf_get_by_data(src_mmap, src_data);
        
            auto dst_data = std::vector<char>(4096);
            auto dst_mmap = shoc::memory_map { dev, dst_data };
            auto dst_mid = dst_data.begin() + 2048;

            auto compressed_buf = buf_inv.buf_get_by_addr(dst_mmap, std::span { dst_data.begin(), dst_mid });
            auto decompressed_buf = buf_inv.buf_get_by_addr(dst_mmap, std::span { dst_mid, dst_data.end() });

            auto ctx = co_await engine->create_context<shoc::compress_context>(dev, 1);

            auto checksums = shoc::compress_checksums {};
            auto compress_status = co_await ctx->compress(src_buf, compressed_buf, &checksums);

            CO_ASSERT_EQ(DOCA_SUCCESS, compress_status, std::string { "compression failed: " } + doca_error_get_descr(compress_status));
            CO_ASSERT_GT(compressed_buf.data().size(), 0, "compressed data is empty");
            CO_ASSERT_LT(compressed_buf.data().size(), src_data.size(), "compressed data is larger than source data");
            CO_ASSERT_EQ(checksums.crc, 4025347724, fmt::format("unexpected crc checksum during compression, crc = {}", checksums.crc));
            CO_ASSERT_EQ(checksums.adler, 2629515667, fmt::format("unexpected adler checksum during compression, adler = {}", checksums.adler));

            auto decompress_status = co_await ctx->decompress(compressed_buf, decompressed_buf);

            CO_ASSERT_EQ(DOCA_SUCCESS, decompress_status, std::string { "decompression failed: " } + doca_error_get_descr(decompress_status));
            CO_ASSERT(std::ranges::equal(src_data, decompressed_buf.data()), "decompressed data is different from source data");
        } catch(shoc::doca_exception &e) {
            // Bluefield 3 has no compression device, only decompression
            if(e.doca_error() == DOCA_ERROR_NOT_FOUND) {
                co_return;
            }
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }
    };

    auto task = [](
        auto fiber_fn,
        std::string *report
    ) -> boost::cobalt::task<void> {
        auto engine = shoc::progress_engine{};
        fiber_fn(&engine, report);
        co_await engine.run();
    } (
        fiber_fn,
        &report
    );

    boost::cobalt::run(std::move(task));

    ASSERT_EQ("", report);
}
