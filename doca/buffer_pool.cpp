#include "buffer_pool.hpp"

#include "error.hpp"

namespace doca {
    buffer_pool::buffer_pool(
        device &dev,
        std::size_t num_elements,
        std::size_t element_size,
        std::size_t element_alignment
    ):
        handle_(doca_buf_pool_destroy),
        memory_(num_elements * element_size),
        mmap_(dev, memory_)
    {
        doca_buf_pool *pool;
        enforce_success(doca_buf_pool_create(num_elements, element_size, mmap_.handle(), &pool));
        handle_.reset(pool);

        enforce_success(doca_buf_pool_set_element_alignment(handle_.handle(), element_alignment));
        enforce_success(doca_buf_pool_start(handle_.handle()));
    }

    auto buffer_pool::num_elements() const -> std::uint32_t {
        std::uint32_t result;
        enforce_success(doca_buf_pool_get_num_elements(handle_.handle(), &result));
        return result;
    }

    auto buffer_pool::num_free_elements() const -> std::uint32_t {
        std::uint32_t result;
        enforce_success(doca_buf_pool_get_num_free_elements(handle_.handle(), &result));
        return result;
    }

    auto buffer_pool::allocate_buffer() -> buffer {
        doca_buf *buf_handle;
        enforce_success(doca_buf_pool_buf_alloc(handle_.handle(), &buf_handle));
        return { buf_handle };
    }

    auto buffer_pool::allocate_buffer(std::size_t data_length, std::size_t data_offset) -> buffer {
        auto buf = allocate_buffer();
        buf.set_data(data_length, data_offset);

        return { buf };
    }
}
