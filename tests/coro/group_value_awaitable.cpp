#include <shoc/coro/value_awaitable.hpp>
#include <boost/cobalt.hpp>
#include <gtest/gtest.h>

namespace {
    struct fiber {
        struct promise_type {
            auto get_return_object() const noexcept { return fiber{}; }
            auto unhandled_exception() const noexcept {
                try {
                    std::rethrow_exception(std::current_exception());
                } catch(std::exception &e) {
                    EXPECT_TRUE(false) << "fiber exited with error: " << e.what();
                } catch(...) {
                    EXPECT_TRUE(false) << "fiber exited with unknown error";
                }
            }

            auto return_void() const noexcept {}
            auto initial_suspend() const noexcept { return std::suspend_never{}; }
            auto final_suspend() const noexcept { return std::suspend_never{}; }
        };
    };

    auto do_wait(
        shoc::coro::value_awaitable<int> &awaitable,
        int expected,
        bool *checkpoint
    ) -> fiber {
        auto x = co_await awaitable;
        EXPECT_EQ(x, expected);
        *checkpoint = true;
    }

    auto do_wait_for_error(
        shoc::coro::value_awaitable<int> &awaitable,
        doca_error_t expected,
        bool *checkpoint
    ) -> fiber {
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

    do_wait(awaitable, 42, &checkpoint);

    ASSERT_TRUE(checkpoint);
}

TEST(docapp_coro_value_awaitable, plain_value_suspended) {
    auto awaitable = shoc::coro::value_awaitable<int>::create_space();
    bool checkpoint = false;

    do_wait(awaitable, 42, &checkpoint);

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
