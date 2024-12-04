#include "memory_map.hpp"
#include "error.hpp"
#include "logger.hpp"

#include <doca_mmap.h>

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
}
