#pragma once

#include <stdexcept>
#include <doca_error.h>
#include <doca_log.h>

namespace doca {
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

    inline auto enforce_success(doca_error result, doca_error expected = DOCA_SUCCESS) -> void {
        if(result != expected) {
            throw doca_exception(result);
        }
    }
}
