#include <shoc/error.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/coro/task.hpp>

#include <gtest/gtest.h>

TEST(doca_coro_task, lazy_task) {
    int count = 0;

    auto task_fn = [](int *ctr) -> shoc::coro::lazy_task<int> {
        ++*ctr;
        co_return 42;
    };

    auto fiber_fn = [](
        int *ctr,
        auto (*task_fn)(int *ctr) -> shoc::coro::lazy_task<int>
    ) -> shoc::coro::fiber {
        auto task = task_fn(ctr);

        EXPECT_EQ(*ctr, 0);

        auto val = co_await task;

        EXPECT_EQ(val, 42);
        EXPECT_EQ(*ctr, 1);
    };

    fiber_fn(&count, task_fn);

    EXPECT_EQ(count, 1);
}

TEST(doca_coro_task, eager_task) {
    int count = 0;

    auto task_fn = [](int *ctr) -> shoc::coro::eager_task<int> {
        ++*ctr;
        co_return 42;
    };

    auto fiber_fn = [](
        int *ctr,
        auto (*task_fn)(int *ctr) -> shoc::coro::eager_task<int>
    ) -> shoc::coro::fiber {
        auto task = task_fn(ctr);

        EXPECT_EQ(*ctr, 1);

        auto val = co_await task;

        ++*ctr;

        EXPECT_EQ(val, 42);
        EXPECT_EQ(*ctr, 2);
    };

    fiber_fn(&count, task_fn);

    EXPECT_EQ(count, 2);
}

TEST(doca_coro_task, except) {
    int count = 0;

    auto task_fn = [](int *ctr) -> shoc::coro::eager_task<int> {
        ++*ctr;

        throw shoc::doca_exception(DOCA_ERROR_UNKNOWN);

        co_return 42;
    };

    auto fiber_fn = [](
        int *ctr,
        auto (*task_fn)(int *ctr) -> shoc::coro::eager_task<int>
    ) -> shoc::coro::fiber {
        try {
            [[maybe_unused]] auto val = co_await task_fn(ctr);
        } catch(shoc::doca_exception &e) {
            EXPECT_EQ(*ctr, 1);
            ++*ctr;
        }
    };

    fiber_fn(&count, task_fn);

    EXPECT_EQ(count, 2);
}
