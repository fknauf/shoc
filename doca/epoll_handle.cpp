#include "epoll_handle.hpp"
#include "error.hpp"
#include "logger.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <ranges>

namespace doca {
    epoll_handle::epoll_handle():
        fd_{epoll_create1(EPOLL_CLOEXEC)}
    {
    }

    epoll_handle::~epoll_handle() {
        close();
    }

    epoll_handle::epoll_handle(epoll_handle &&other):
        fd_(other.fd_)
    {
        other.fd_ = -1;
    }


    epoll_handle &epoll_handle::operator=(epoll_handle &&other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }

    auto epoll_handle::close() -> void {
        if(fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    auto epoll_handle::add_event_source(int event_fd) -> void {
        epoll_event events_in = { EPOLLIN, { .fd = event_fd } };

        if(0 != epoll_ctl(fd_, EPOLL_CTL_ADD, event_fd, &events_in)) {
            throw doca_error(DOCA_ERROR_OPERATING_SYSTEM);
        }
    }

    auto epoll_handle::wait(int timeout_ms) const -> int {
        epoll_event ep_event = {0, { 0 } };
        auto nfd = epoll_wait(fd_, &ep_event, 1, timeout_ms);

        if(nfd == -1) {
            throw doca_error(DOCA_ERROR_OPERATING_SYSTEM);
        }

        // Workaround: can't use ep_event members directly because they sometimes have
        // __attribute__((bitwise)) and can't be bound to a reference.
        std::uint32_t events = ep_event.events;
        std::uint64_t event_data = ep_event.data.u64;
        logger->trace("epoll_handle: wait done. events = {}, u64 = {}", events, event_data);

        return nfd == 0 ? -1 : ep_event.data.fd;
    }
}
