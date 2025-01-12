#pragma once

#include <unistd.h>

#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <utility>

namespace shoc {
    class file_descriptor {
    public:
        static constexpr int invalid_handle = -1;

        file_descriptor() = default;
        file_descriptor(int fd):
            fd_ { fd }
        {}

        ~file_descriptor() {
            clear();
        }

        file_descriptor(file_descriptor const &) = delete;
        file_descriptor(file_descriptor &&other) {
            swap(other);
        }

        file_descriptor &operator=(file_descriptor const &) = delete;
        file_descriptor &operator=(file_descriptor &&other) {
            clear();
            swap(other);
            return *this;
        }

        auto swap(file_descriptor &other) -> void {
            std::swap(fd_, other.fd_);
        }

        auto posix_handle() const { return fd_; }

        auto read(void *buf, std::size_t len) const {
            return ::read(fd_, buf, len);
        }

        auto write(void const *buf, std::size_t len) const {
            return ::write(fd_, buf, len);
        }

    private:
        auto clear() -> void {
            if(fd_ >= 0) {
                ::close(fd_);
                fd_ = invalid_handle;
            }
        }

        int fd_ = invalid_handle;
    };

    class event_counter {
    public:
        event_counter();

        auto pop() -> std::uint64_t;
        auto increase(std::uint64_t val = 1) -> void;

        auto eventfd() const { return fd_.posix_handle(); }

    private:
        file_descriptor fd_;
    };

    class duration_timer {
    public:
        duration_timer() = default;
        duration_timer(std::chrono::microseconds us);

        auto pop() -> std::uint64_t;
        auto timerfd() const { return fd_.posix_handle(); }

    private:
        file_descriptor fd_;
    };
}
