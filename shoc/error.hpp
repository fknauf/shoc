#pragma once

#include <stdexcept>
#include <doca_error.h>
#include <doca_log.h>

#include <algorithm>

namespace shoc {
    class doca_exception: public std::runtime_error {
    public:
        doca_exception(doca_error_t err):
            runtime_error(doca_error_get_descr(err)),
            err_(err)
        {}

        auto doca_error() const noexcept {
            return err_;
        }

    private:
        doca_error_t err_;
    };

    inline auto enforce(bool condition, doca_error_t err) -> void {
        if(!condition) {
            [[unlikely]] throw doca_exception(err);
        }
    }

    inline auto enforce_success(doca_error_t result, doca_error_t expected = DOCA_SUCCESS) -> void {
        enforce(result == expected, result);
    }

    inline auto enforce_success(doca_error_t result, std::initializer_list<int> expected) -> void {
        if(std::ranges::count(expected, result) == 0) {
            [[unlikely]] throw doca_exception(result);
        }
    }
}
