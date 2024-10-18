#pragma once

#include <doca_error.h>

#include <exception>

namespace doca::coro {
    /**
     * Common base class for receptables for error reporting regardless of
     * return value
     */
    struct error_receptable {
        virtual ~error_receptable() = default;

        virtual auto set_exception(std::exception_ptr ex) -> void = 0;
        virtual auto set_error(doca_error_t err) -> void = 0;
    };
}
