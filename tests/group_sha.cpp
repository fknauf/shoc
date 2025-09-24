#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/device.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/sha.hpp>

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

TEST(docapp_sha, hash) {
    auto report = std::string { "fiber not started" };

    shoc::logger->set_level(spdlog::level::warn);

    auto fiber_fn = [](
        shoc::progress_engine_lease engine,
        std::string *report
    ) -> boost::cobalt::detached {
        try {
            *report = "";

            shoc::logger->info("starting fiber");

            auto dev = shoc::device::find(shoc::device_capability::sha);

            shoc::logger->info("device found");

            auto buf_inv = shoc::buffer_inventory { 2 };

            auto src_data = std::string { 
                "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt"
                " ut labore et dolore magna aliquyam erat, sed diam voluptua."
            };
            auto src_mmap = shoc::memory_map { dev, src_data };
            auto src_buf = buf_inv.buf_get_by_data(src_mmap, src_data);
        
            auto dst_data = std::vector<char>(4096);
            auto dst_mmap = shoc::memory_map { dev, dst_data };
            auto dst_buf = buf_inv.buf_get_by_addr(dst_mmap, dst_data);

            auto ctx = co_await shoc::sha_context::create(engine, dev, 1);

            shoc::logger->info("context created");

            auto err = co_await ctx->hash(DOCA_SHA_ALGORITHM_SHA256, src_buf, dst_buf);
            CO_ASSERT_EQ(DOCA_SUCCESS, err, std::string { "hashing failed: " } + doca_error_get_descr(err));
            shoc::logger->info("hash finished");

            auto expected = std::string {
                "\xa1\xf5\xa9\x67\x75\xb4\x7c\xe3\x2f\xf5\xce\xc6\x84\x2f\xd4\x3f"
                "\x4a\xea\x81\x8e\xce\xca\x7b\xde\x5c\xa7\xf3\x69\xac\xef\x71\x84"
            };

            auto expected_size = expected.size();
            auto hash_size = dst_buf.data().size();
            CO_ASSERT_EQ(hash_size, expected_size, fmt::format("hash size unexpected: {} != {}", hash_size, expected_size));

            CO_ASSERT(std::ranges::equal(dst_buf.data<char>(), expected), "wrong hash");
        } catch(shoc::doca_exception &e) {
            if(e.doca_error() == DOCA_ERROR_NOT_FOUND) {
                // crypto-disabled bluefield
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
