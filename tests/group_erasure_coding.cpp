#include <shoc/aligned_memory.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/device.hpp>
#include <shoc/erasure_coding.hpp>
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

TEST(docapp_erasure_coding, create_and_recover) {
    auto report = std::string { "fiber not started" };

    shoc::logger->set_level(spdlog::level::warn);
    //shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);

    auto fiber_fn = [](
        shoc::progress_engine *engine,
        std::string *report
    ) -> boost::cobalt::detached {
        try {
            *report = "";

            shoc::logger->info("starting fiber");

            auto dev = shoc::device::find(shoc::device_capability::erasure_coding);
            shoc::logger->info("device found");

            auto ctx = co_await engine->create_context<shoc::ec_context>(dev, 2);
            shoc::logger->info("context created");

            std::uint32_t const blocksize = 64;
            std::uint32_t const payload_block_count = 3;
            std::uint32_t const rdnc_block_count = 2;

            auto coding_matrix = ctx->coding_matrix(DOCA_EC_MATRIX_TYPE_CAUCHY, payload_block_count, rdnc_block_count);
            shoc::logger->info("created coding matrix");

            auto buf_inv = shoc::buffer_inventory { 2 };

            auto rdnc_blocks = shoc::aligned_blocks { rdnc_block_count, blocksize };
            auto payload_blocks = shoc::aligned_blocks { payload_block_count, blocksize };

            auto payload_message = std::string {
                "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed dia"
                "m nonumy eirmod tempor invidunt ut labore et dolore magna aliquy"
                "am erat, sed diam voluptua."
            };
            payload_blocks.assign(payload_message);

            {
                auto rdnc_mem = rdnc_blocks.as_writable_bytes();
                auto rdnc_mmap = shoc::memory_map { dev, rdnc_mem };
                auto rdnc_buf = buf_inv.buf_get_by_addr(rdnc_mmap, rdnc_mem);
                auto payload_mem = payload_blocks.as_writable_bytes();
                auto payload_mmap = shoc::memory_map { dev, payload_mem };
                auto payload_buf = buf_inv.buf_get_by_data(payload_mmap, payload_mem);

                auto err = co_await ctx->create(coding_matrix, payload_buf, rdnc_buf);
                CO_ASSERT_EQ(DOCA_SUCCESS, err, std::string { "redundancy block creation failed: " } + doca_error_get_descr(err));
            }
    
            shoc::logger->info("created redundancy blocks");

            auto missing_indices = std::vector<std::uint32_t>{ 0, 2 };
            auto recovered_blocks = shoc::aligned_blocks(2, blocksize);
            auto partial_blocks = shoc::aligned_blocks(payload_block_count + rdnc_block_count - 2, blocksize);
            std::ranges::copy(payload_blocks.block(1), partial_blocks.writable_block(0).begin());
            std::ranges::copy(rdnc_blocks.block(0), partial_blocks.writable_block(1).begin());
            std::ranges::copy(rdnc_blocks.block(1), partial_blocks.writable_block(2).begin());

            {
                auto recover_matrix = ctx->recover_matrix(coding_matrix, missing_indices);

                auto partial_mem = partial_blocks.as_writable_bytes();
                auto partial_mmap = shoc::memory_map { dev, partial_mem };
                auto partial_buf = buf_inv.buf_get_by_data(partial_mmap, partial_mem);
                auto recovered_mem = recovered_blocks.as_writable_bytes();
                auto recovered_mmap = shoc::memory_map { dev, recovered_mem };
                auto recovered_buf = buf_inv.buf_get_by_addr(recovered_mmap, recovered_mem);

                auto err = co_await ctx->recover(recover_matrix, partial_buf, recovered_buf);
                CO_ASSERT_EQ(DOCA_SUCCESS, err, std::string { "recovery failed: " } + doca_error_get_descr(err));
            }

            CO_ASSERT(std::ranges::equal(payload_blocks.block(0), recovered_blocks.block(0)), "first recovered block contains bad data");
            CO_ASSERT(std::ranges::equal(payload_blocks.block(2), recovered_blocks.block(1)), "second recovered block contains bad data");
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
