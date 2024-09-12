#pragma once

#include "error.hpp"

#include <doca_buf.h>

#include <memory>
#include <span>
#include <vector>
#include <utility>

namespace doca {
    /**
     * reference-counted buffer in a DOCA memory map. It has an outer and inner memory range, one
     * for available mapped memory and one for used memory:
     * 
     * | head space | data space | tail space |
     * 
     * The head space is sometimes used by DOCA for header information, e.g. a zlib header in some
     * compression modes. For output buffers, the length of the data space has to be 0. For source
     * buffers, the data space describes the memory area where the source data resides.
     * 
     * For example: if you want to compress a file in chunks of 1MiB with zlib compatibility, you
     * would usually have a source buffer of 1 MB size where the data space encompasses the whole
     * buffer (except for the last chunk, which might be shorter), and an output buffer where the
     * head space is large enough to hold the ZLIB header followed by a data space of length zero.
     */
    class buffer
    {
    public:
        buffer(doca_buf *handle = nullptr);

        [[nodiscard]] auto handle() const noexcept -> doca_buf* { return ref_.get(); }
        [[nodiscard]] auto has_value() const noexcept -> bool { return handle() != nullptr; }

        /**
         * data region
         */
        template<typename Byte = char>
        [[nodiscard]] auto data() -> std::span<Byte> 
            requires (sizeof(Byte) == 1)
        {
            return obtain_data<Byte>();
        }

        template<typename Byte = char>
        [[nodiscard]] auto data() const -> std::span<Byte const>
            requires (sizeof(Byte) == 1)
        {
            return obtain_data<Byte const>();
        }

        /**
         * Memory region of the full buffer (including header and footer space)
         */
        template<typename Byte = char>
        [[nodiscard]] auto memory() -> std::span<Byte> 
            requires (sizeof(Byte) == 1)
        {
            return obtain_memory<Byte>();
        }

        template<typename Byte = char>
        [[nodiscard]] auto memory() const -> std::span<Byte const>
            requires (sizeof(Byte) == 1)
        {
            return obtain_memory<Byte const>();
        }

        /**
         * Set the length and optionally offset of the data region within the memory region.
         */
        auto set_data(std::size_t data_len, std::size_t data_offset = 0) -> std::span<char>;
        auto clear() -> void;

    private:
        template<typename Byte>
        [[nodiscard]] auto obtain_data() const -> std::span<Byte>
            requires (sizeof(Byte) == 1)
        {
            void *raw_base = nullptr;
            std::size_t len = 0;

            enforce_success(doca_buf_get_data(handle(), &raw_base));
            enforce_success(doca_buf_get_data_len(handle(), &len));

            auto base = static_cast<Byte*>(raw_base);

            return { base, base + len };
        }

        template<typename Byte>
        [[nodiscard]] auto obtain_memory() const -> std::span<Byte>
            requires (sizeof(Byte) == 1)
        {
            void *raw_base = nullptr;
            std::size_t len = 0;

            enforce_success(doca_buf_get_head(handle(), &raw_base));
            enforce_success(doca_buf_get_len(handle(), &len));

            auto base = static_cast<Byte*>(raw_base);

            return { base, base + len };
        }

        // shared_ptr instead of doca_buf_inc_refcount/doca_buf_dec_refcount because
        // those aren't thread-safe.
        std::shared_ptr<doca_buf> ref_;
    };
}
