#include <shoc/coro/fiber.hpp>
#include <shoc/coro/value_awaitable.hpp>

#include <asio/asio.hpp>
#include <gtest/gtest.h>


namespace {
    auto do_wait(
        shoc::coro::value_awaitable<int> &awaitable,
        int expected,
        bool *checkpoint
    ) -> asio::awaitable<void> {
        auto x = co_await awaitable;
        EXPECT_EQ(x, expected);
        *checkpoint = true;
    }

    auto do_wait_for_error(
        shoc::coro::value_awaitable<int> &awaitable,
        doca_error_t expected,
        bool *checkpoint
    ) -> asio::awaitable<void> {
        try {
            co_await awaitable;
        } catch(shoc::doca_exception &e) {
            EXPECT_EQ(e.doca_error(), expected);
            *checkpoint = true;
        }
    }
}

TEST(docapp_coro_value_awaitable, plain_value_precomputed) {
    auto awaitable = shoc::coro::value_awaitable<int>::from_value(42);
    bool checkpoint = false;

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, plain_value_suspended) {
    auto io = asio::io_context { 1}
    auto awaitable = shoc::coro::value_awaitable<int>::create_space();
    bool checkpoint = false;

    ASSERT_FALSE(checkpoint);

    awaitable.receptable_ptr()->emplace_value(42);
    awaitable.receptable_ptr()->resume();

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, error_precomputed) {
    auto awaitable = shoc::coro::value_awaitable<int>::from_error(DOCA_ERROR_NOT_CONNECTED);
    bool checkpoint = false;

    do_wait_for_error(awaitable, DOCA_ERROR_NOT_CONNECTED, &checkpoint);

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, error_suspended) {
    auto awaitable = shoc::coro::value_awaitable<int>::create_space();
    bool checkpoint = false;

    do_wait_for_error(awaitable, DOCA_ERROR_NOT_CONNECTED, &checkpoint);

    ASSERT_FALSE(checkpoint);

    awaitable.receptable_ptr()->set_error(DOCA_ERROR_NOT_CONNECTED);
    awaitable.receptable_ptr()->resume();

    ASSERT_TRUE(checkpoint);
}
