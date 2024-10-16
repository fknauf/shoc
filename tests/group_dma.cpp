#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/device.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <gtest/gtest.h>

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

TEST(docapp_dma, local_copy) {
    auto engine = doca::progress_engine {};
    auto report = std::string { "fiber not started" };

    [](
        doca::progress_engine *engine,
        std::string *report
    ) -> doca::coro::fiber {
        try {
            *report = "";

            auto dev = doca::device::find_by_capabilities(doca::device_capability::dma);
            auto ctx = co_await engine->create_context<doca::dma_context>(dev, 1);

            auto buf_inv = doca::buffer_inventory { 2 };

            auto src_data = std::string { "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua." };
            auto src_mmap = doca::memory_map { dev, src_data };
            auto src_buf = buf_inv.buf_get_by_data(src_mmap, src_data);

            auto dst_data = std::vector<char>(4096);
            auto dst_mmap = doca::memory_map { dev, dst_data };
            auto dst_buf = buf_inv.buf_get_by_addr(dst_mmap, dst_data);

            auto status = co_await ctx->memcpy(src_buf, dst_buf);

            CO_ASSERT_EQ(DOCA_SUCCESS, status, std::string { "dma memcpy failed: " } + doca_error_get_descr(status));
            CO_ASSERT(std::ranges::equal(src_data, dst_buf.data()), "destination data is different from source data");
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }
    } (
        &engine,
        &report
    );

    engine.main_loop();

    ASSERT_EQ("", report);
}
