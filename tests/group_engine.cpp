#include <doca/progress_engine.hpp>

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

enum {
    YIELDS_OF_FIBER_1,
    YIELDS_OF_FIBER_2,
    WAKEUPS_OF_FIBER_1,
    WAKEUPS_OF_FIBER_2,
    FINISHED_FIBER_1,
    FINISHED_FIBER_2,
    COUNTER_COUNT
};

#define CO_ASSERT_YIELDS(y1, y2) \
    do { \
        CO_ASSERT_EQ((y1), (counters[YIELDS_OF_FIBER_1]), fmt::format("unexpected yields in fiber 1: {} != {}", (y1), (counters[YIELDS_OF_FIBER_1]))); \
        CO_ASSERT_EQ((y2), (counters[YIELDS_OF_FIBER_2]), fmt::format("unexpected yields in fiber 2: {} != {}", (y2), (counters[YIELDS_OF_FIBER_2]))); \
    } while(false)

#define CO_ASSERT_WAKEUPS(w1, w2) \
    do { \
        CO_ASSERT_EQ((w1), (counters[WAKEUPS_OF_FIBER_1]), fmt::format("unexpected wakeups in fiber 1: {} != {}", (w1), (counters[WAKEUPS_OF_FIBER_1]))); \
        CO_ASSERT_EQ((w2), (counters[WAKEUPS_OF_FIBER_2]), fmt::format("unexpected wakeups in fiber 2: {} != {}", (w2), (counters[WAKEUPS_OF_FIBER_2]))); \
    } while(false)

TEST(docapp_engine, yielding) {
    auto engine = doca::progress_engine {};
    auto report1 = std::string { "not started" };
    auto report2 = std::string { "not started" };

    int counters[COUNTER_COUNT] = { 0, 0, 0, 0, 0, 0 };

    [&, report = &report1]() -> doca::coro::fiber {
        try {
            *report = "";

            CO_ASSERT_YIELDS(0, 0);
            CO_ASSERT_WAKEUPS(0, 0);

            ++counters[YIELDS_OF_FIBER_1];
            co_await engine.yield();
            ++counters[WAKEUPS_OF_FIBER_1];

            CO_ASSERT_YIELDS(1, 1);
            CO_ASSERT_WAKEUPS(1, 0);
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[FINISHED_FIBER_1] = 1;
    } ();

    [&, report = &report2]() -> doca::coro::fiber {
        try {
            *report = "";

            CO_ASSERT_YIELDS(1, 0);
            CO_ASSERT_WAKEUPS(0, 0);

            ++counters[YIELDS_OF_FIBER_2];
            co_await engine.yield();
            ++counters[WAKEUPS_OF_FIBER_2];
            
            CO_ASSERT_YIELDS(1, 1);
            CO_ASSERT_WAKEUPS(1, 1);
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }

        counters[FINISHED_FIBER_2] = 1;
    } ();

    engine.main_loop_while([&] {
        return 
            counters[FINISHED_FIBER_1] == 0 || 
            counters[FINISHED_FIBER_2] == 0;
    });

    EXPECT_EQ("", report1);
    EXPECT_EQ("", report2);
    EXPECT_EQ(counters[YIELDS_OF_FIBER_1], 1);
    EXPECT_EQ(counters[YIELDS_OF_FIBER_2], 1);
    EXPECT_EQ(counters[WAKEUPS_OF_FIBER_1], 1);
    EXPECT_EQ(counters[WAKEUPS_OF_FIBER_2], 1);
}
