#pragma once

#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "coro/value_awaitable.hpp"
#include "device.hpp"
#include "memory_map.hpp"
#include "progress_engine.hpp"
#include "unique_handle.hpp"

#include <doca_devemu_pci.h>
#include <doca_devemu_pci_type.h>

#include <cstdint>

namespace shoc::devemu {
    enum emulated_device_state {
        power_off,
        hot_plug_in_progress,
        power_on,
        hot_unplug_in_progress
    };

    class pci_type;

    auto cap_is_mmap_add_dev_supported(doca_devinfo *dev) -> bool;

    class pci_type {
    public:
        pci_type(char const *name);
        ~pci_type();

        [[nodiscard]] auto handle() const -> doca_devemu_pci_type* { return handle_.get(); }

        [[nodiscard]] auto is_hotplug_supported(doca_devinfo *dev) const -> bool;
        [[nodiscard]] auto is_mgmt_supported(doca_devinfo *dev) const -> bool;

        [[nodiscard]] auto hotplug_device_predicate() const {
            return [this](doca_devinfo *dev) -> bool { return is_hotplug_supported(dev); };
        }

        [[nodiscard]] auto mgmt_device_predicate() const {
            return [this](doca_devinfo *dev) -> bool { return is_mgmt_supported(dev); };
        }

        auto set_dev(device dev) -> pci_type &;
        auto set_device_id(std::uint16_t device_id) -> pci_type &;
        auto set_vendor_id(std::uint16_t vendor_id) -> pci_type&;
        auto set_subsystem_id(std::uint16_t subsystem_id) -> pci_type&;
        auto set_subsystem_vendor_id(std::uint16_t subsystem_vid) -> pci_type&;
        auto set_revision_id(std::uint8_t revision_id) -> pci_type &;
        auto set_class_code(std::uint32_t class_code) -> pci_type &;
        auto set_num_msix(std::uint16_t num_msix) -> pci_type&;
        auto set_num_db(std::uint16_t num_db) -> pci_type&;
        
        auto set_memory_bar_conf(
            std::uint8_t id,
            std::uint8_t log_sz,
            doca_devemu_pci_bar_mem_type memory_type,
            bool prefetchable
        ) -> pci_type &;

        auto set_io_bar_conf(
            std::uint8_t id,
            std::uint8_t log_sz
        ) -> pci_type &;

        auto set_bar_db_region_by_offset_conf(
            std::uint8_t id,
            std::uint64_t start_addr,
            std::uint64_t size,
            std::uint8_t log_db_size,
            std::uint8_t log_stride_size
        ) -> pci_type &;

        auto set_bar_db_region_by_data_conf(
            std::uint8_t id,
            std::uint64_t start_addr,
            std::uint64_t size,
            std::uint8_t log_db_size,
            std::uint16_t db_id_msbyte,
            std::uint16_t db_id_lsbyte
        ) -> pci_type &;

        auto set_bar_msix_table_region_conf(
            std::uint8_t id,
            std::uint64_t start_addr,
            std::uint64_t size
        ) -> pci_type &;

        auto set_bar_msix_pba_region_conf(
            std::uint8_t id,
            std::uint64_t start_addr,
            std::uint64_t size
        ) -> pci_type &;

        auto set_bar_stateful_region_conf(
            std::uint8_t id,
            std::uint64_t start_addr,
            std::uint64_t size            
        ) -> pci_type &;

        [[nodiscard]] auto start() -> doca_error_t;
        [[nodiscard]] auto stop() -> doca_error_t;

        [[nodiscard]] auto is_started() -> bool;

        auto create_representor() -> device_representor;

    private:
        unique_handle<doca_devemu_pci_type, doca_devemu_pci_type_destroy> handle_;
        device dev_;
    };

    class pci_dev:
        public context<
            doca_devemu_pci_dev,
            doca_devemu_pci_dev_destroy,
            doca_devemu_pci_dev_as_ctx,
            true
        >
    {
    public:
        pci_dev(
            progress_engine *parent,
            pci_type const &type,
            device_representor rep
        );

        [[nodiscard]] static auto create(
            progress_engine_lease &engine,
            pci_type const &type,
            device_representor rep
        ) {
            return engine.create_context<pci_dev>(type, std::move(rep));
        }

        [[nodiscard]] auto hotplug_state() const -> doca_devemu_pci_hotplug_state;

        [[nodiscard]] auto remote_mmap(
            std::initializer_list<device> devices,
            std::span<std::byte> memory,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ) -> memory_map;

        [[nodiscard]] auto remote_mmap(
            device dev,
            std::span<std::byte> memory,
            std::uint32_t permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE
        ) -> memory_map {
            return remote_mmap({ dev }, memory, permissions);
        }

        auto hotplug() -> coro::value_awaitable<doca_devemu_pci_hotplug_state>;
        auto hotunplug() -> coro::value_awaitable<doca_devemu_pci_hotplug_state>;

    private:
        static auto hotplug_state_changed_callback(
            doca_devemu_pci_dev *pci_dev,
            doca_data user_data
        ) -> void;

        device_representor rep_;

        coro::value_receptable<doca_devemu_pci_hotplug_state> *hot_plug_waiter_ = nullptr;
        coro::value_receptable<doca_devemu_pci_hotplug_state> *hot_unplug_waiter_ = nullptr;
    };
}
