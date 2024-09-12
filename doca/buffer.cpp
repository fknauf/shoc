#include "buffer.hpp"
#include "logger.hpp"

#include <cstdint>

namespace doca {
    buffer::buffer(doca_buf *handle):
        ref_ { 
            handle, 
            [](doca_buf* handle){ 
                if(handle != nullptr) { 
                    doca_buf_dec_refcount(handle, nullptr);
                } 
            } 
        }
    {}

    auto buffer::set_data(std::size_t data_len, std::size_t data_offset) -> std::span<char> {
        auto old_data = data();
        auto mem = memory();

        enforce_success(doca_buf_set_data(handle(), mem.data() + data_offset, data_len));

        return old_data;
    }

    auto buffer::clear() -> void {
        ref_.reset();
    }
}
