#pragma once

#include "unique_handle.hpp"
#include "buffer.hpp"
#include "device.hpp"
#include "memory_map.hpp"

#include <doca_buf_pool.h>

#include <cstdint>

namespace shoc {
    /**
     * Pool of equally-sized buffers in a single memory mapping. Owns the memory mapping and the mapped memory
     * for ease of use.
     * 
     * Needs to live longer than the buffers it allocates.
     */
    class buffer_pool {
    public:
        /**
         * @param dev device to which the memory will be mapped.
         * @param num_elements number of buffers in the pool
         * @param element_size size of each buffer's memory region
         * @param element_alignment min alignment of the buffers' memory
         */
        buffer_pool(
            device &dev,
            std::size_t num_elements,
            std::size_t element_size,
            std::size_t element_alignment = 1);

        [[nodiscard]] auto num_elements() const -> std::uint32_t;
        [[nodiscard]] auto num_free_elements() const -> std::uint32_t;

        /**
         * Allocates an empty buffer (i.e., data region of length 0 at offset 0)
         */
        [[nodiscard]] auto allocate_buffer() -> buffer;

        /**
         * Allocates a buffer with its data region set to a specific size and offset
         */
        [[nodiscard]] auto allocate_buffer(std::size_t data_length, std::size_t data_offset = 0) -> buffer;

    private:
        unique_handle<doca_buf_pool, doca_buf_pool_destroy> handle_;
        std::vector<std::uint8_t> memory_;
        memory_map mmap_;
    };
}