#pragma once

#include <doca/logger.hpp>

#include <coroutine>

namespace doca::coro {
    struct fiber {
        struct promise_type {
            auto get_return_object() const noexcept { return fiber{}; }
            auto unhandled_exception() const noexcept {
                try {
                    std::rethrow_exception(std::current_exception());
                } catch(std::exception &e) {
                    logger->warn("fiber exited with error: {}", e.what());
                } catch(...) {
                    logger->warn("fiber exited with unknown error");
                }
            }

            auto return_void() const noexcept {}
            auto initial_suspend() const noexcept { return std::suspend_never{}; }
            auto final_suspend() const noexcept { return std::suspend_never{}; }
        };
    };
}
