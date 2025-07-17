#include "devemu_pci.hpp"
#include "error.hpp"
#include "progress_engine.hpp"

namespace shoc::devemu {
    pci_type::pci_type(
        char const *name
    ) {
        doca_devemu_pci_type *raw_handle;
        enforce_success(doca_devemu_pci_type_create(name, &raw_handle));
        handle_.reset(raw_handle);
    }

    pci_type::~pci_type() {
        auto err = stop();

        if(err != DOCA_SUCCESS) {
            logger->error("Stopping of PCI type in destructor failed: {}", doca_error_get_descr(err));
        }
    }

    auto pci_type::is_hotplug_supported(doca_devinfo *dev) const -> bool {
        std::uint8_t supported = 0;
        auto err = doca_devemu_pci_cap_type_is_hotplug_supported(dev, handle(), &supported);
        return err == DOCA_SUCCESS && supported != 0;
    }

    auto pci_type::is_mgmt_supported(doca_devinfo *dev) const -> bool {
        std::uint8_t supported = 0;
        auto err = doca_devemu_pci_cap_type_is_mgmt_supported(dev, handle(), &supported);
        return err == DOCA_SUCCESS && supported != 0;
    }

    auto pci_type::set_dev(device dev) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_dev(handle(), dev.handle()));
        return *this;
    }

    auto pci_type::set_device_id(std::uint16_t device_id) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_device_id(handle(), device_id));
        return *this;
    }

    auto pci_type::set_vendor_id(std::uint16_t vendor_id) -> pci_type& {
        enforce_success(doca_devemu_pci_type_set_vendor_id(handle(), vendor_id));
        return *this;
    }

    auto pci_type::set_subsystem_id(std::uint16_t subsystem_id) -> pci_type& {
        enforce_success(doca_devemu_pci_type_set_subsystem_id(handle(), subsystem_id));
        return *this;
    }

    auto pci_type::set_subsystem_vendor_id(std::uint16_t subsystem_vid) -> pci_type& {
        enforce_success(doca_devemu_pci_type_set_subsystem_vendor_id(handle(), subsystem_vid));
        return *this;
    }

    auto pci_type::set_revision_id(std::uint8_t revision_id) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_revision_id(handle(), revision_id));
        return *this;
    }

    auto pci_type::set_class_code(std::uint32_t class_code) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_class_code(handle(), class_code));
        return *this;
    }

    auto pci_type::set_num_msix(std::uint16_t num_msix) -> pci_type& {
        enforce_success(doca_devemu_pci_type_set_num_msix(handle(), num_msix));
        return *this;
    }

    auto pci_type::set_num_db(std::uint16_t num_db) -> pci_type& {
        enforce_success(doca_devemu_pci_type_set_num_db(handle(), num_db));
        return *this;
    }

    auto pci_type::set_memory_bar_conf(
        std::uint8_t id,
        std::uint8_t log_sz,
        doca_devemu_pci_bar_mem_type memory_type,
        bool prefetchable
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_memory_bar_conf(
            handle(),
            id,
            log_sz,
            memory_type,
            static_cast<std::uint8_t>(prefetchable)
        ));
        return *this;
    }

    auto pci_type::set_io_bar_conf(
        std::uint8_t id,
        std::uint8_t log_sz
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_io_bar_conf(
            handle(),
            id,
            log_sz
        ));
        return *this;
    }

    auto pci_type::set_bar_db_region_by_offset_conf(
        std::uint8_t id,
        std::uint64_t start_addr,
        std::uint64_t size,
        std::uint8_t log_db_size,
        std::uint8_t log_stride_size
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_bar_db_region_by_offset_conf(
            handle(),
            id,
            start_addr,
            size,
            log_db_size,
            log_stride_size
        ));
        return *this;
    }

    auto pci_type::set_bar_db_region_by_data_conf(
        std::uint8_t id,
        std::uint64_t start_addr,
        std::uint64_t size,
        std::uint8_t log_db_size,
        std::uint16_t db_id_msbyte,
        std::uint16_t db_id_lsbyte
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_bar_db_region_by_data_conf(
            handle(),
            id,
            start_addr,
            size,
            log_db_size,
            db_id_msbyte,
            db_id_lsbyte
        ));
        return *this;
    }

    auto pci_type::set_bar_msix_table_region_conf(
        std::uint8_t id,
        std::uint64_t start_addr,
        std::uint64_t size
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_bar_msix_table_region_conf(
            handle(),
            id,
            start_addr,
            size
        ));
        return *this;
    }

    auto pci_type::set_bar_msix_pba_region_conf(
        std::uint8_t id,
        std::uint64_t start_addr,
        std::uint64_t size
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_bar_msix_pba_region_conf(
            handle(),
            id,
            start_addr,
            size
        ));
        return *this;
    }

    auto pci_type::set_bar_stateful_region_conf(
        std::uint8_t id,
        std::uint64_t start_addr,
        std::uint64_t size
    ) -> pci_type & {
        enforce_success(doca_devemu_pci_type_set_bar_stateful_region_conf(
            handle(),
            id,
            start_addr,
            size
        ));
        return *this;
    }

    auto pci_type::start() -> doca_error_t {
        return doca_devemu_pci_type_start(handle());
    }

    auto pci_type::stop() -> doca_error_t {
        return is_started() ? doca_devemu_pci_type_stop(handle()) : DOCA_SUCCESS;
    }

    auto pci_type::is_started() -> bool {
        std::uint8_t started;
        auto err = doca_devemu_pci_type_is_started(handle(), &started);
        return err == DOCA_SUCCESS && started != 0;
    }

    auto pci_type::create_representor() -> device_representor {
        doca_dev_rep *rep;
        enforce_success(doca_devemu_pci_dev_create_rep(handle(), &rep));
        return { rep, detail::doca_destroyer<doca_devemu_pci_dev_destroy_rep>{} };
    }

    pci_dev::pci_dev(
        progress_engine *engine,
        pci_type const &type,
        device_representor rep
    ):
        context {
            engine,
            context::create_doca_handle<doca_devemu_pci_dev_create>(type.handle(), rep.handle(), engine->handle())
        },
        rep_ { rep }
    {
        doca_data ctx_user_data = { .ptr = this };
        enforce_success(doca_devemu_pci_dev_event_hotplug_state_change_register(
            handle(),
            &hotplug_state_changed_callback,
            ctx_user_data
        ));
    }

    auto pci_dev::hotplug_state() const -> doca_devemu_pci_hotplug_state {
        doca_devemu_pci_hotplug_state hotplug_state;
        enforce_success(doca_devemu_pci_dev_get_hotplug_state(handle(), &hotplug_state));
        return hotplug_state;
    }

    auto pci_dev::hotplug_state_changed_callback(
        [[maybe_unused]] doca_devemu_pci_dev *pci_dev_handle,
        doca_data user_data
    ) -> void try {
        auto self = static_cast<pci_dev*>(user_data.ptr);
        auto hp_state = self->hotplug_state();

        switch(hp_state) {
        case DOCA_DEVEMU_PCI_HP_STATE_POWER_OFF:
            if(self->hot_unplug_waiter_ != nullptr) {
                self->hot_unplug_waiter_->set_value(std::move(hp_state));
            }
            break;
        case DOCA_DEVEMU_PCI_HP_STATE_POWER_ON:
            if(self->hot_plug_waiter_ != nullptr) {
                self->hot_plug_waiter_->set_value(std::move(hp_state));
            }
            break;
        default:
            break;
        }
    } catch(doca_exception &e) {
        logger->error("hotplug state change handler failed: {}", e.what());
    }

    auto pci_dev::hotplug() -> coro::value_awaitable<doca_devemu_pci_hotplug_state> {
        if(hot_plug_waiter_ != nullptr) {
            throw doca_exception(DOCA_ERROR_BAD_STATE);
        }

        auto result = coro::value_awaitable<doca_devemu_pci_hotplug_state>::create_space();
        auto receptable = result.receptable_ptr();

        enforce_success(doca_devemu_pci_dev_hotplug(handle()));

        hot_plug_waiter_ = receptable;
        return result;
    }

    auto pci_dev::hotunplug() -> coro::value_awaitable<doca_devemu_pci_hotplug_state> {
        if(hot_unplug_waiter_ != nullptr) {
            throw doca_exception(DOCA_ERROR_BAD_STATE);
        }

        auto result = coro::value_awaitable<doca_devemu_pci_hotplug_state>::create_space();
        auto receptable = result.receptable_ptr();

        enforce_success(doca_devemu_pci_dev_hotunplug(handle()));

        hot_unplug_waiter_ = receptable;
        return result;
    }

    auto pci_dev::remote_mmap(
        std::initializer_list<device> devices,
        std::span<std::byte> memory,
        std::uint32_t permissions
    ) -> memory_map {
        doca_mmap *raw_mmap;
        enforce_success(doca_devemu_pci_mmap_create(handle(), &raw_mmap));
        unique_handle<doca_mmap, doca_mmap_destroy> mmap { raw_mmap };
        raw_mmap = nullptr;
        
        enforce_success(doca_mmap_set_max_num_devices(mmap.get(), devices.size()));

        for(auto &dev : devices) {
            enforce_success(doca_mmap_add_dev(mmap.get(), dev.handle()));
        }

        enforce_success(doca_mmap_set_permissions(mmap.get(), permissions));
        enforce_success(doca_mmap_set_memrange(mmap.get(), memory.data(), memory.size()));
        enforce_success(doca_mmap_start(mmap.get()));

        return { std::move(mmap), true };
    }
}
