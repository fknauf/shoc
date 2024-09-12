#include "buffer_inventory.hpp"

#include "error.hpp"
#include <doca_buf_inventory.h>
#include <cstdint>

namespace doca {
    buffer_inventory::buffer_inventory(std::uint32_t max_bufs)
    {
        doca_buf_inventory *inv;

        enforce_success(doca_buf_inventory_create(max_bufs, &inv));
        handle_.reset(inv);

        doca_data user_data;
        user_data.ptr = static_cast<void*>(this);
        enforce_success(doca_buf_inventory_set_user_data(handle_.handle(), user_data));
        enforce_success(doca_buf_inventory_start(handle_.handle()));
    }

    auto buffer_inventory::buf_get_by_args(memory_map &mmap, void *addr, std::size_t len, void *data, std::size_t data_len) -> buffer {
        doca_buf *dest;
        enforce_success(doca_buf_inventory_buf_get_by_args(handle_.handle(), mmap.handle(), addr, len, data, data_len, &dest));
        return dest;
    }

    auto buffer_inventory::buf_get_by_addr(memory_map &mmap, void *addr, std::size_t len) -> buffer {
        doca_buf *dest;
        enforce_success(doca_buf_inventory_buf_get_by_addr(handle_.handle(), mmap.handle(), addr, len, &dest));
        return dest;
    }

    auto buffer_inventory::buf_get_by_data(memory_map &mmap, void *data, std::size_t data_len) -> buffer {
        doca_buf *dest;
        enforce_success(doca_buf_inventory_buf_get_by_data(handle_.handle(), mmap.handle(), data, data_len, &dest));
        return dest;
    }

    auto buffer_inventory::buf_dup(buffer const &src) -> buffer {
        doca_buf *dest;
        enforce_success(doca_buf_inventory_buf_dup(handle_.handle(), src.handle(), &dest));
        return dest;
    }

    auto buffer_inventory::get_num_elements() const -> std::uint32_t {
        std::uint32_t dest;
        enforce_success(doca_buf_inventory_get_num_elements(handle_.handle(), &dest));
        return dest;
    }

    auto buffer_inventory::get_num_free_elements() const -> std::uint32_t {
        std::uint32_t dest;
        enforce_success(doca_buf_inventory_get_num_free_elements(handle_.handle(), &dest));
        return dest;
    }
}
