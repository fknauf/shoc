#include "buffer.hpp"
#include "logger.hpp"

#include <cstdint>

namespace shoc {
    buffer::buffer(doca_buf *handle):
        handle_ { handle }
    {}

    buffer::~buffer() {
        if(handle_ != nullptr) {
            doca_buf_dec_refcount(handle_, nullptr);
        }
    }

    buffer::buffer(buffer const &other):
        handle_(other.handle())
    {
        doca_buf_inc_refcount(handle_, nullptr);
    }

    buffer::buffer(buffer &&other):
        handle_(std::exchange(other.handle_, nullptr))
    {
    }

    auto buffer::operator=(buffer const& other) -> buffer& {
        buffer copy { other };
        swap(copy);
        return *this;
    }

    auto buffer::operator=(buffer &&other) -> buffer& {
        buffer copy { std::move(other) };
        swap(copy);
        return *this;
    }

    auto buffer::set_data(std::size_t data_len, std::size_t data_offset) -> std::span<char> {
        auto old_data = data();
        auto mem = memory();

        enforce_success(doca_buf_set_data(handle(), mem.data() + data_offset, data_len));

        return old_data;
    }

    auto buffer::clear() -> void {
        if(handle_ != nullptr) {
            doca_buf_dec_refcount(handle_, nullptr);
            handle_ = nullptr;
        }
    }
}
