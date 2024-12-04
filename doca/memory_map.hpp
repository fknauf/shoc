#pragma once

#include "buffer.hpp"
#include "common/raw_memory.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_mmap.h>

#include <cstdint>
#include <span>

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
            std::initializer_list<device> devices,
            void *base,
            std::size_t len,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        memory_map(
            std::initializer_list<device> devices,
            std::span<std::byte> range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        memory_map(
            std::initializer_list<device> devices,
            non_cv_byte_range auto &&range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ):
            memory_map { devices, create_span<std::byte>(std::forward<decltype(range)>(range)), permissions }
        {}

        memory_map(
            device dev,
            non_cv_byte_range auto &&range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ): 
            memory_map(std::initializer_list{dev}, std::forward<decltype(range)>(range), permissions)
        {}

        memory_map(
            device const &dev,
            export_descriptor export_desc
        );

        /**
         * @return the managed doca_mmap handle
         */
        [[nodiscard]] auto handle() const {
            return handle_.get();
        }

        /**
         * @return the mapped memory region
         */
        [[nodiscard]] auto span() const -> std::span<std::byte const> {
            return range_;
        }
        
        [[nodiscard]] auto span() -> std::span<std::byte> {
            return range_;
        }

        template<byteish OutByte>
        [[nodiscard]] auto span_as() const {
            return reinterpret_span<OutByte const>(range_);
        }
        
        template<byteish OutByte>
        [[nodiscard]] auto span_as() {
            return reinterpret_span<OutByte>(range_);
        }

        [[nodiscard]] auto export_pci(device const &dev) const -> export_descriptor;

    private:
        unique_handle<doca_mmap, doca_mmap_destroy> handle_;
        std::span<std::byte> range_;
    };
}
