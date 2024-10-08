#include "memory_map.hpp"
#include "error.hpp"
#include "logger.hpp"

#include <doca_mmap.h>

namespace doca {
    memory_map::memory_map(
        device const &dev,
        void *base, std::size_t len,
        std::uint32_t permissions
    ):
        memory_map(dev, {static_cast<std::uint8_t*>(base), len}, permissions)
    {}

    memory_map::memory_map(
        device const &dev,
        std::span<char> range,
        std::uint32_t permissions
    ):
        memory_map(dev, range.data(), range.size(), permissions)
    {}

    memory_map::memory_map(
        device const &dev,
        std::span<std::uint8_t> range,
        std::uint32_t permissions
    ):
        range_(range)
    {
        logger->debug("mapping base = {}, size = {}", static_cast<void*>(range_.data()), range_.size());

        doca_mmap *map = nullptr;

        enforce_success(doca_mmap_create(&map));
        handle_.reset(map);

        enforce_success(doca_mmap_set_memrange(handle_.handle(), range_.data(), range_.size()));
        enforce_success(doca_mmap_add_dev(handle_.handle(), dev.handle()));
        enforce_success(doca_mmap_set_permissions(handle_.handle(), permissions));
        enforce_success(doca_mmap_start(handle_.handle()));
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
        enforce_success(doca_mmap_get_memrange(handle_.handle(), &range_base, &range_length));
        range_ = std::span { reinterpret_cast<std::uint8_t*>(range_base), range_length };
    }

    auto memory_map::export_pci(device const &dev) const -> export_descriptor {
        void const *export_desc = nullptr;
        std::size_t export_len = 0;

        enforce_success(doca_mmap_export_pci(handle_.handle(), dev.handle(), &export_desc, &export_len));

        return {
            .base_ptr = export_desc,
            .length = export_len
        };
    }
}
