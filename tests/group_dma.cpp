#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/device.hpp>
#include <shoc/dma.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

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
    auto engine = shoc::progress_engine {};
    auto report = std::string { "fiber not started" };

    [](
        shoc::progress_engine *engine,
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";

            auto dev = shoc::device::find_by_capabilities(shoc::device_capability::dma);
            auto ctx = co_await engine->create_context<shoc::dma_context>(dev, 1);

            CO_ASSERT_EQ(shoc::context_state::running, ctx->state(), "state not running after acquiry");

            auto buf_inv = shoc::buffer_inventory { 2 };

            auto src_data = std::string { "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua." };
            auto src_mmap = shoc::memory_map { dev, src_data };
            auto src_buf = buf_inv.buf_get_by_data(src_mmap, src_data);

            auto dst_data = std::vector<char>(4096);
            auto dst_mmap = shoc::memory_map { dev, dst_data };
            auto dst_buf = buf_inv.buf_get_by_addr(dst_mmap, dst_data);

            auto status = co_await ctx->memcpy(src_buf, dst_buf);

            CO_ASSERT_EQ(DOCA_SUCCESS, status, std::string { "dma memcpy failed: " } + doca_error_get_descr(status));
            CO_ASSERT(std::ranges::equal(src_data, dst_buf.data()), "destination data is different from source data");

            auto stop_awaitable = ctx->stop();

            CO_ASSERT_EQ(shoc::context_state::stopping, ctx->state(), "state not stopping after stop()");

            co_await stop_awaitable;

            CO_ASSERT_EQ(shoc::context_state::idle, ctx->state(), "state not idle after waiting for stoppage");
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
