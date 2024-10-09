#pragma once

#include <cstdint>
#include <chrono>

namespace doca {
    class event_counter {
    public:
        event_counter();
        ~event_counter();

        auto pop() -> std::uint64_t;
        auto increase(std::uint64_t val = 1) -> void;

        auto eventfd() const { return fd_; }

    private:
        int fd_;
    };

    class duration_timer {
    public:
        duration_timer(std::chrono::microseconds us);
        ~duration_timer();

        auto pop() -> std::uint64_t;
        auto timerfd() const { return fd_; }

    private:
        int fd_;
    };
}
