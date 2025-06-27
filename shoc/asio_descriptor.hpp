#pragma once

#include "error.hpp"
#include "logger.hpp"

#include <doca_pe.h>

#include <boost/asio/posix/descriptor.hpp>

namespace shoc {
    /**
     * Boost.Asio file descriptor for compatibility with its epoll mechanism. Needed for integration
     * with boost.cobalt when busy polling is not desirable.
     *
     * The main difference from the descriptor type in ASIO is that we don't want to close the file
     * descriptor in the destructor and release it before the parent class would do that.
     */
    template<typename InnerExecutor>
    class asio_descriptor:
        public boost::asio::posix::basic_descriptor<InnerExecutor>
    {
    public:
        using base_type = boost::asio::posix::basic_descriptor<InnerExecutor>;
        using base_type::base_type;

        ~asio_descriptor() {
            base_type::release();
        }
    };
}
