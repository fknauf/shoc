#pragma once

#include "unique_handle.hpp"
#include "buffer.hpp"
#include "device.hpp"

#include <doca_mmap.h>

#include <cstdint>
#include <span>
#include <vector>

namespace doca {
    /**
     * Memory map between host and a doca device. May be extended in the future to cover
     * more than one device.
     * 
     * This is mainly a RAII class that handles the lifetime of a doca_mmap handle; in addition to that
     * it owns the mapped memory.
     */
    class memory_map
    {
    public:
        struct export_descriptor {
            void const *base_ptr;
            std::size_t length;
        };

        /**
         * @param dev the device that'll have access to this memory
         * @param size size of the memory to allocate and map
         */
        memory_map(
            device const &dev,
            void *base,
            std::size_t len,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        memory_map(
            device const &dev,
            std::span<std::uint8_t> range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        memory_map(
            device const &dev,
            std::span<char> range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        memory_map(
            device const &dev,
            export_descriptor export_desc
        );

        /**
         * @return the managed doca_mmap handle
         */
        [[nodiscard]] auto handle() const { return handle_.handle(); }

        /**
         * @return the mapped memory region
         */
        [[nodiscard]] auto span() const -> std::span<std::uint8_t const> { return range_; }
        [[nodiscard]] auto span() -> std::span<std::uint8_t> { return range_; }

        [[nodiscard]] auto export_pci(device const &dev) const -> export_descriptor;

    private:
        unique_handle<doca_mmap> handle_ { doca_mmap_destroy };
        std::span<std::uint8_t> range_;
    };
}
