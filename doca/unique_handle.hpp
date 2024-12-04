#pragma once

#include <doca_error.h>

#include <memory>
#include <utility>

namespace doca {
    namespace detail {
        template<auto DestroyFunction>
        struct doca_destroyer {
            auto operator()(auto handle) const -> void {
                if(handle != nullptr) {
                    DestroyFunction(handle);
                }
            }
        };
    }

    template<typename Handle, auto Deleter>
    using unique_handle = std::unique_ptr<Handle, detail::doca_destroyer<Deleter>>;
}
