#pragma once

#include <doca/error.hpp>
#include <doca/memory_map.hpp>

#include <algorithm>
#include <cstring>
#include <span>

struct remote_buffer_descriptor {
    std::span<char const> src_range;
    doca::memory_map::export_descriptor export_desc;

    remote_buffer_descriptor(
        std::span<char const> src,
        doca::memory_map::export_descriptor ex
    ):
        src_range { src },
        export_desc { ex }
    { }

    remote_buffer_descriptor(std::string const &msg) {
        doca::enforce(msg.size() > 16, DOCA_ERROR_UNEXPECTED);

        auto src_buffer_addr = std::uint64_t{};
        auto src_buffer_len = std::uint64_t{};

        std::memcpy(&src_buffer_addr, msg.data(), 8);
        std::memcpy(&src_buffer_len, msg.data() + 8, 8);

        src_range = std::span {
            reinterpret_cast<char const*>(src_buffer_addr),
            src_buffer_len
        };

        export_desc = doca::memory_map::export_descriptor {
            .base_ptr = msg.data() + 16,
            .length = msg.size() - 16
        };
    }

    auto format() const -> std::string {
        auto result = std::string(16 + export_desc.length, ' ');

        auto src_base = reinterpret_cast<std::uint64_t>(src_range.data());
        auto src_length = src_range.size();

        std::memcpy(result.data(), &src_base, 8);
        std::memcpy(result.data() + 8, &src_length, 8);

        auto export_base = reinterpret_cast<char const *>(export_desc.base_ptr);
        auto export_end = export_base + export_desc.length;
        std::copy(export_base, export_end, result.data() + 16);

        return result;
    }
};
