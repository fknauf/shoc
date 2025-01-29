#pragma once

#include "error.hpp"
#include "logger.hpp"

#include <doca_pe.h>

#include <boost/asio/posix/descriptor.hpp>

namespace shoc {
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
