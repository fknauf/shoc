#include <shoc/progress_engine.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

#define CO_FAIL(message) do { *report = (message); co_return; } while(false)
#define CO_ASSERT(condition, message) do { if(!(condition)) { CO_FAIL(message); } } while(false)

#define CO_ASSERT_EQ(val1, val2, message) CO_ASSERT((val1) == (val2), message)
#define CO_ASSERT_NE(val1, val2, message) CO_ASSERT((val1) != (val2), message)
#define CO_ASSERT_LT(val1, val2, message) CO_ASSERT((val1) <  (val2), message)
#define CO_ASSERT_LE(val1, val2, message) CO_ASSERT((val1) <= (val2), message)
#define CO_ASSERT_GT(val1, val2, message) CO_ASSERT((val1) >  (val2), message)
#define CO_ASSERT_GE(val1, val2, message) CO_ASSERT((val1) >= (val2), message)

namespace {
    class fiber_counters {
    public:
        auto finished() const noexcept { return finished_; }
        auto yields() const noexcept { return yields_; }
        auto wakeups() const noexcept { return wakeups_; }

        auto inc_yields() noexcept { ++yields_; }
        auto inc_wakeups() noexcept  { ++wakeups_; }
        auto finish() noexcept { finished_ = true; }

    private:
        bool finished_ = false;
        int yields_ = 0;
        int wakeups_ = 0;
    };
}

#define CO_ASSERT_YIELDS(y1, y2) \
    do { \
        CO_ASSERT_EQ((y1), counters[0].yields(), fmt::format("unexpected yields in fiber 1: {} != {}", (y1), counters[0].yields())); \
        CO_ASSERT_EQ((y2), counters[1].yields(), fmt::format("unexpected yields in fiber 2: {} != {}", (y2), counters[1].yields())); \
    } while(false)

#define CO_ASSERT_WAKEUPS(w1, w2) \
    do { \
        CO_ASSERT_EQ((w1), counters[0].wakeups(), fmt::format("unexpected wakeups in fiber 1: {} != {}", (w1), counters[0].wakeups())); \
        CO_ASSERT_EQ((w2), counters[1].wakeups(), fmt::format("unexpected wakeups in fiber 2: {} != {}", (w2), counters[1].wakeups())); \
    } while(false)

TEST(docapp_engine, yielding) {
    auto engine = shoc::progress_engine {};
    auto report1 = std::string { "not started" };
    auto report2 = std::string { "not started" };

    fiber_counters counters[2];

    [](
        shoc::progress_engine *engine,
        fiber_counters counters[2],
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";

            CO_ASSERT_YIELDS(0, 0);
            CO_ASSERT_WAKEUPS(0, 0);

            counters[0].inc_yields();
            co_await engine->yield();
            counters[0].inc_wakeups();

            CO_ASSERT_YIELDS(1, 1);
            CO_ASSERT_WAKEUPS(1, 0);
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[0].finish();
    } (
        &engine,
        counters,
        &report1
    );

    [](
        shoc::progress_engine *engine,
        fiber_counters counters[2],
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";

            CO_ASSERT_YIELDS(1, 0);
            CO_ASSERT_WAKEUPS(0, 0);

            counters[1].inc_yields();
            co_await engine->yield();
            counters[1].inc_wakeups();

            CO_ASSERT_YIELDS(1, 1);
            CO_ASSERT_WAKEUPS(1, 1);
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[1].finish();
    } (
        &engine,
        counters,
        &report2
    );

    engine.main_loop();

    EXPECT_TRUE(counters[0].finished());
    EXPECT_TRUE(counters[1].finished());
    EXPECT_EQ("", report1);
    EXPECT_EQ("", report2);
    EXPECT_EQ(counters[0].yields(), 1);
    EXPECT_EQ(counters[1].yields(), 1);
    EXPECT_EQ(counters[0].wakeups(), 1);
    EXPECT_EQ(counters[1].wakeups(), 1);
}

TEST(docapp_engine, timeouts) {
    using namespace std::chrono_literals;

    auto engine = shoc::progress_engine {};
    auto report1 = std::string { "not started" };
    auto report2 = std::string { "not started" };

    fiber_counters counters[2];

    [](
        shoc::progress_engine *engine,
        fiber_counters counters[2],
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";

            counters[0].inc_yields();
            co_await engine->yield();
            counters[0].inc_wakeups();

            auto start = std::chrono::system_clock::now();

            counters[0].inc_yields();
            co_await engine->timeout(10ms);
            counters[0].inc_wakeups();

            auto end = std::chrono::system_clock::now();
            auto delta_t = end - start;
            CO_ASSERT_GE(delta_t, 10ms, fmt::format("timeout too short: {}", delta_t));
            CO_ASSERT_LT(delta_t, 15ms, fmt::format("timeout too long: {}", delta_t));

            CO_ASSERT_YIELDS(2, 2);
            CO_ASSERT_WAKEUPS(2, 2);
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[0].finish();
    } (
        &engine,
        counters,
        &report1
    );

    [](
        shoc::progress_engine *engine,
        fiber_counters counters[2],
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";
            counters[1].inc_yields();
            co_await engine->yield();
            counters[1].inc_wakeups();

            auto start = std::chrono::system_clock::now();

            counters[1].inc_yields();
            co_await engine->timeout(5ms);
            counters[1].inc_wakeups();

            CO_ASSERT_YIELDS(2, 2);
            CO_ASSERT_WAKEUPS(1, 2);

            auto end = std::chrono::system_clock::now();
            auto delta_t = end - start;
            CO_ASSERT_GE(delta_t, 5ms, fmt::format("timeout too short: {}", delta_t));
            CO_ASSERT_LT(delta_t, 10ms, fmt::format("timeout too long: {}", delta_t));
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[1].finish();
    } (
        &engine,
        counters,
        &report2
    );

    engine.main_loop();

    EXPECT_TRUE(counters[0].finished());
    EXPECT_TRUE(counters[1].finished());
    EXPECT_EQ("", report1);
    EXPECT_EQ("", report2);
    EXPECT_EQ(counters[0].yields(), 2);
    EXPECT_EQ(counters[1].yields(), 2);
    EXPECT_EQ(counters[0].wakeups(), 2);
    EXPECT_EQ(counters[1].wakeups(), 2);
}
