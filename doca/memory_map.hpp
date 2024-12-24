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
        /**
         * Descriptor required to gain access to remotely-exported memory mappings
         *
         * It's a type of its own instead of a std::span<std::byte const> to make the
         * corresponding constructor's purpose more explicit
         */
        struct export_descriptor {
            void const *base_ptr;
            std::size_t length;
        };

        /**
         * @param devices list of devices that'll have access to this memory
         * @param range memory region to map
         * @param permissions access permissions as per DOCA API
         */
        memory_map(
            std::initializer_list<device> devices,
            std::span<std::byte> range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        );

        /**
         * @param devices list of devices that'll have access to this memory
         * @param range memory region to map as a contiguous range of std::byte, char, or uint8_t
         * @param permissions access permissions as per DOCA API
         */
        memory_map(
            std::initializer_list<device> devices,
            non_cv_byte_range auto &&range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ):
            memory_map { devices, create_span<std::byte>(std::forward<decltype(range)>(range)), permissions }
        {}

        /**
         * Convenience constructor for mappings involving a single device
         *
         * @param device the device that'll have access to this memory
         * @param range memory region to map as a contiguous range of std::byte, char, or uint8_t
         * @param permissions access permissions as per DOCA API
         */
        memory_map(
            device dev,
            non_cv_byte_range auto &&range,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ):
            memory_map(std::initializer_list{dev}, std::forward<decltype(range)>(range), permissions)
        {}

        /**
         * Gain access to a remotely-exported memory map
         *
         * @param dev device that exported the mmap
         * @param export_desc export descriptor obtained from export_pci (on the remote side)
         */
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

        /**
         * For convenience: Look at the underlying memory region as an array of bytes,
         * chars, or std::uint8_ts depending on OutByte parameter
         *
         * @param OutByte one of std::byte, char, or std::uint8_t
         * @return the mapped memory region as span of OutBytes
         */
        template<byteish OutByte>
        [[nodiscard]] auto span_as() const {
            return reinterpret_span<OutByte const>(range_);
        }

        template<byteish OutByte>
        [[nodiscard]] auto span_as() {
            return reinterpret_span<OutByte>(range_);
        }

        /**
         * Export the memory map to PCIe
         *
         * @param dev Device to export to
         * @return export descriptor for import on the remote side
         */
        [[nodiscard]] auto export_pci(device const &dev) const -> export_descriptor;

    private:
        unique_handle<doca_mmap, doca_mmap_destroy> handle_;
        std::span<std::byte> range_;
    };
}
