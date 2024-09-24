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
}
