#pragma once

#include <doca_types.h>

namespace shoc {
    /**
     * RAII wrapper around a file descriptor for epolling.
     */
    class epoll_handle
    {
    public:
        epoll_handle();
        ~epoll_handle();

        epoll_handle(epoll_handle const &) = delete;
        epoll_handle(epoll_handle &&);

        epoll_handle &operator=(epoll_handle const &) = delete;
        epoll_handle &operator=(epoll_handle &&);

        auto close() -> void;
        auto add_event_source(int event_fd) -> void;

        [[nodiscard]]
        auto wait(int timeout_ms = -1) const -> int;

    private:
        int fd_ = -1;
    };
}
