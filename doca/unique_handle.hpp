#pragma once

#include <doca_error.h>

#include <utility>

namespace doca {
    /**
     * RAII wrapper around DOCA handles for automatic cleanup. Moveable, not copyable.
     * 
     * Takes inspiration from std::unique_ptr, but is more limited. The deleter is generally going
     * to be a doca_*_destroy function of some kind and passed in at run time for simplicity.
     * May in the future be refactored to static deleter binding to save the space for the function
     * pointer.
     */
    template<typename Handle, auto Deleter>
    class unique_handle {
    public:
        unique_handle() = default;

        ~unique_handle() {
            clear();
        }

        unique_handle(unique_handle const &) = delete;
        unique_handle(unique_handle &&other) {
            *this = std::move(other);
        }

        unique_handle &operator=(unique_handle const &) = delete;
        unique_handle &operator=(unique_handle &&other) {
            reset(other.handle_);
            other.handle_ = nullptr;
            return *this;
        }

        [[nodiscard]] auto handle() const noexcept {
            return handle_;
        }

        auto reset(Handle *new_handle) -> void {
            clear();
            handle_ = new_handle;
        }

        auto clear() -> void {
            if(handle_ != nullptr) {
                Deleter(handle_);
                handle_ = nullptr;
            }
        }

    private:
        Handle *handle_ { nullptr };
    };
}
