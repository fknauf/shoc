#pragma once

#include "error.hpp"
#include "logger.hpp"

#include <doca_pe.h>

#include <asio/any_io_executor.hpp>
#include <asio/posix/descriptor.hpp>
#include <asio/strand.hpp>
#include <system_error>

namespace shoc {
    template<typename InnerExecutor = asio::any_io_executor>
    class asio_descriptor:
        public asio::posix::basic_descriptor<asio::strand<InnerExecutor>>
    {
    public:
        using base_type = asio::posix::basic_descriptor<asio::strand<InnerExecutor>>;
        using executor_type = asio::strand<InnerExecutor>;

        using base_type::base_type;

        ~asio_descriptor() {
            base_type::release();
        }
    };
}
