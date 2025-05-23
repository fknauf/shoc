#pragma once

#include "error.hpp"

#include <doca_buf.h>

#include <memory>
#include <span>
#include <vector>
#include <utility>

namespace shoc {
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
        /**
         * takes ownership of the supplied handle (unless it is nullptr), that is: the created
         * buffer object will decrease the reference counter when it is destroyed.
         *
         * @param handle pointer to a raw doca_buf instance
         */
        buffer(doca_buf *handle = nullptr);

        ~buffer();

        buffer(buffer const &);
        buffer(buffer &&);

        auto operator=(buffer const&) -> buffer&;
        auto operator=(buffer &&) -> buffer&;

        auto swap(buffer &other) -> void {
            std::swap(handle_, other.handle_);
        }

        [[nodiscard]] auto handle() const noexcept -> doca_buf* { return handle_; }
        [[nodiscard]] auto has_value() const noexcept -> bool { return handle() != nullptr; }

        /**
         * data region
         */
        template<typename Byte = char>
        [[nodiscard]] auto data() const -> std::span<Byte>
            requires (sizeof(Byte) == 1)
        {
            void *raw_base = nullptr;
            std::size_t len = 0;

            enforce_success(doca_buf_get_data(handle(), &raw_base));
            enforce_success(doca_buf_get_data_len(handle(), &len));

            auto base = static_cast<Byte*>(raw_base);

            return { base, base + len };
        }

        /**
         * Memory region of the full buffer (including header and footer space)
         */
        template<typename Byte = char>
        [[nodiscard]] auto memory() const -> std::span<Byte>
            requires (sizeof(Byte) == 1)
        {
            void *raw_base = nullptr;
            std::size_t len = 0;

            enforce_success(doca_buf_get_head(handle(), &raw_base));
            enforce_success(doca_buf_get_len(handle(), &len));

            auto base = static_cast<Byte*>(raw_base);

            return { base, base + len };
        }

        /**
         * Set the length and optionally offset of the data region within the memory region.
         */
        auto set_data(std::size_t data_len, std::size_t data_offset = 0) -> std::span<char>;
        auto clear() -> void;

    private:
        doca_buf *handle_ = nullptr;
    };
}
