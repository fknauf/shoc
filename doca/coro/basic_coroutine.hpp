#pragma once

#include <coroutine>

namespace doca::coro {
    struct basic_coroutine {
        struct promise_type {
            auto get_return_object() const noexcept { return basic_coroutine{}; }
            auto unhandled_exception() const noexcept {}
            auto return_void() const noexcept {}
            auto initial_suspend() const noexcept { return std::suspend_never{}; }
            auto final_suspend() const noexcept { return std::suspend_never{}; }
        };
    };
}
