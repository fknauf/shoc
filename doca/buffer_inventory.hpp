#pragma once

#include "unique_handle.hpp"
#include "buffer.hpp"
#include "device.hpp"
#include "memory_map.hpp"

#include <doca_buf.h>
#include <doca_buf_inventory.h>

#include <cstdint>
#include <vector>

namespace doca {
    /**
     * buffer inventory that references existing memory maps.
     *
     * referenced memory maps need to live longer than the buffer inventory, while the
     * buffer inventory needs to live longer than the buffers it creates.
     */
    class buffer_inventory
    {
    public:
        buffer_inventory(std::uint32_t max_buf);

        [[nodiscard]] auto buf_get_by_args(memory_map &mmap, void const *addr, std::size_t len, void const *data, std::size_t data_len) -> buffer;
        [[nodiscard]] auto buf_get_by_addr(memory_map &mmap, void const *addr, std::size_t len) -> buffer;
        [[nodiscard]] auto buf_get_by_data(memory_map &mmap, void const *data, std::size_t data_len) -> buffer;

        [[nodiscard]] auto buf_dup(buffer const &src) -> buffer;

        [[nodiscard]] auto buf_get_by_args(memory_map &mmap, std::span<char const> mem, std::span<char const> data) -> buffer {
            return buf_get_by_args(mmap, mem.data(), mem.size(), data.data(), data.size());
        }

        [[nodiscard]] auto buf_get_by_addr(memory_map &mmap, std::span<char const> mem) -> buffer {
            return buf_get_by_addr(mmap, mem.data(), mem.size());
        }

        [[nodiscard]] auto buf_get_by_data(memory_map &mmap, std::span<char const> data) -> buffer {
            return buf_get_by_data(mmap, data.data(), data.size());
        }

        [[nodiscard]] auto get_num_elements() const -> std::uint32_t;
        [[nodiscard]] auto get_num_free_elements() const -> std::uint32_t;

    private:
        unique_handle<doca_buf_inventory> handle_ { doca_buf_inventory_destroy };
    };
}
