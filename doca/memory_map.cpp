#include "memory_map.hpp"
#include "error.hpp"
#include "logger.hpp"

#include <doca_mmap.h>

#include <sstream>

namespace doca {
    memory_map::memory_map(
        std::initializer_list<device> devices,
        std::span<std::byte> range,
        std::uint32_t permissions
    ):
        range_(range)
    {
        logger->debug("mapping base = {}, size = {}", static_cast<void*>(range_.data()), range_.size());

        doca_mmap *map = nullptr;

        enforce_success(doca_mmap_create(&map));
        handle_.reset(map);

        auto u8range = reinterpret_span<std::uint8_t>(range);
        enforce_success(doca_mmap_set_memrange(handle(), u8range.data(), u8range.size()));

        for(auto &dev : devices) {
            enforce_success(doca_mmap_add_dev(handle(), dev.handle()));
        }

        enforce_success(doca_mmap_set_permissions(handle(), permissions));
        enforce_success(doca_mmap_start(handle()));
    }

    memory_map::memory_map(
        device const &dev,
        export_descriptor export_desc
    ) {
        doca_mmap *map = nullptr;
        enforce_success(doca_mmap_create_from_export(nullptr, export_desc.base_ptr, export_desc.length, dev.handle(), &map));
        handle_.reset(map);

        void *range_base = nullptr;
        std::size_t range_length = 0;
        enforce_success(doca_mmap_get_memrange(handle(), &range_base, &range_length));
        range_ = create_span<std::byte>(range_base, range_length);
    }

    auto memory_map::export_pci(device const &dev) const -> export_descriptor {
        void const *export_desc = nullptr;
        std::size_t export_len = 0;

        enforce_success(doca_mmap_export_pci(handle(), dev.handle(), &export_desc, &export_len));

        return {
            .base_ptr = export_desc,
            .length = export_len
        };
    }

    auto memory_map::export_descriptor::encode() const -> std::string {
        return fmt::format("{:08x} {:08x}", reinterpret_cast<std::uintptr_t>(base_ptr), length);
    }

    auto memory_map::export_descriptor::decode(std::string const &encoded) -> export_descriptor {
        std::istringstream parser(encoded);
        export_descriptor dest;
        if(parser >> dest) {
            return dest;
        }

        throw doca_exception(DOCA_ERROR_INVALID_VALUE);
    }

    auto operator>>(std::istream &in, memory_map::export_descriptor &dest) -> std::istream & {
        auto oldflags = in.setf(std::ios::hex, std::ios::basefield);
        std::uintptr_t base_addr;
        in >> base_addr >> dest.length;
        in.setf(oldflags);
        dest.base_ptr = reinterpret_cast<void const*>(base_addr);
        return in;
    }
}
