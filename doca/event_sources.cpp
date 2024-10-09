#include "event_sources.hpp"
#include "error.hpp"

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace doca {
    event_counter::event_counter():
        fd_(::eventfd(0, O_NONBLOCK | O_CLOEXEC))
    {
        enforce(fd_.posix_handle() != -1, DOCA_ERROR_OPERATING_SYSTEM);
    }

    auto event_counter::pop() -> std::uint64_t {
        auto val = std::uint64_t(0);

        auto nbytes = fd_.read(&val, sizeof(val));

        if(nbytes == sizeof(val)) {
            return val;
        } else if(nbytes == -1 && errno == EAGAIN) {
            return 0;
        } else {
            throw doca_exception(DOCA_ERROR_OPERATING_SYSTEM);
        }
    }

    auto event_counter::increase(std::uint64_t delta) -> void {
        auto nbytes = fd_.write(&delta, sizeof(delta));
        enforce(nbytes != -1, DOCA_ERROR_OPERATING_SYSTEM);
    }

    duration_timer::duration_timer(std::chrono::microseconds us):
        fd_ { timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC) }
    {
        enforce(fd_.posix_handle() != -1, DOCA_ERROR_OPERATING_SYSTEM);

        itimerspec timerspec = {
            .it_interval = {
                .tv_sec = 0,
                .tv_nsec = 0
            },
            .it_value = {
                .tv_sec  = std::chrono::floor<std::chrono::seconds>(us).count(),
                .tv_nsec = us.count() % 1000000 * 1000
            }
        };

        auto err = timerfd_settime(fd_.posix_handle(), 0, &timerspec, nullptr);
        enforce(err == 0, DOCA_ERROR_OPERATING_SYSTEM);
    }

    auto duration_timer::pop() -> std::uint64_t {
        std::uint64_t val;

        auto nbytes = fd_.read(&val, sizeof(val));

        if(nbytes == sizeof(val)) {
            return val;
        } else if(nbytes == -1 && errno == EAGAIN) {
            return 0;
        } else {
            throw doca_exception(DOCA_ERROR_OPERATING_SYSTEM);
        }
    }
}
