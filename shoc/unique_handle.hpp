#pragma once

#include <doca_error.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace shoc {
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

    template<typename Handle, auto Deleter>
    class shared_handle
    {
    public:
        using element_type = std::remove_extent_t<Handle>;

        shared_handle() = default;

        shared_handle(Handle *handle):
            backend_ { handle, detail::doca_destroyer<Deleter>{} }
        {}

        auto reset() -> void {
            backend_.reset();
        }
        auto reset(Handle *handle) -> void {
            backend_.reset(handle, detail::doca_destroyer<Deleter>{});
        }

        auto swap(shared_handle &other) -> void {
            backend_.swap(other);
        }

        [[nodiscard]] auto get() const noexcept { return backend_.get(); }
        [[nodiscard]] auto operator*() const noexcept { return *backend_; }
        [[nodiscard]] auto operator->() const noexcept { return get(); }
        [[nodiscard]] auto use_count() const noexcept { return backend_.use_count(); }
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(backend_); }
        [[nodiscard]] auto owner_before(shared_handle const &other) const noexcept {
            return backend_.owner_before(other.backend_);
        }

    private:
        std::shared_ptr<Handle> backend_;
    };
}
