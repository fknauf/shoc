#include <doca/coro/fiber.hpp>
#include <doca/coro/value_awaitable.hpp>

#include <gtest/gtest.h>

namespace {
    auto do_wait(doca::coro::value_awaitable<int> &awaitable, int expected, bool *checkpoint) -> doca::coro::fiber {
        auto x = co_await awaitable;
        EXPECT_EQ(x, expected);
        *checkpoint = true;
    }

    auto do_wait_for_error(doca::coro::value_awaitable<int> &awaitable, doca_error_t expected, bool *checkpoint) -> doca::coro::fiber {
        try {
            co_await awaitable;
        } catch(doca::doca_exception &e) {
            EXPECT_EQ(e.doca_error(), expected);
            *checkpoint = true;
        }
    }
}

TEST(docapp_coro_value_awaitable, plain_value_precomputed) {
    auto awaitable = doca::coro::value_awaitable<int>::from_value(42);
    bool checkpoint = false;

    do_wait(awaitable, 42, &checkpoint);

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, plain_value_suspended) {
    auto awaitable = doca::coro::value_awaitable<int>::create_space();
    bool checkpoint = false;

    do_wait(awaitable, 42, &checkpoint);

    ASSERT_FALSE(checkpoint);

    awaitable.dest->emplace_value(42);
    awaitable.dest->resume();

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, error_precomputed) {
    auto awaitable = doca::coro::value_awaitable<int>::from_error(DOCA_ERROR_NOT_CONNECTED);
    bool checkpoint = false;

    do_wait_for_error(awaitable, DOCA_ERROR_NOT_CONNECTED, &checkpoint);

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, error_suspended) {
    auto awaitable = doca::coro::value_awaitable<int>::create_space();
    bool checkpoint = false;

    do_wait_for_error(awaitable, DOCA_ERROR_NOT_CONNECTED, &checkpoint);

    ASSERT_FALSE(checkpoint);

    awaitable.dest->set_error(DOCA_ERROR_NOT_CONNECTED);
    awaitable.dest->resume();

    ASSERT_TRUE(checkpoint);
}
