#pragma once

#include "unique_handle.hpp"
#include "error.hpp"

#include <doca_dev.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace doca {
    class device
    {
    public:
        using tasks_check = auto (doca_devinfo const *) -> doca_error_t;

        device() = default;
        device(device const &) = delete;
        device(device &&) = default;
        device &operator=(device const &) = delete;
        device &operator=(device&&) = default;

        virtual ~device() = default;        

        [[nodiscard]] auto handle() const { return handle_.handle(); }
        [[nodiscard]] auto as_devinfo() const -> doca_devinfo*;

        [[nodiscard]] static auto find_by_pci_addr(std::string const &pci_addr, tasks_check *check_fun = nullptr) -> device;
        [[nodiscard]] static auto find_by_capabilities(tasks_check *check_fun) -> device;

    private:
        device(doca_dev *doca_handle);

        unique_handle<doca_dev> handle_ { doca_dev_close };
    };

    class device_representor {
    public:
        device_representor() = default;
        device_representor(device_representor const &) = delete;
        device_representor(device_representor&&) = default;

        device_representor &operator=(device_representor const &) = delete;
        device_representor &operator=(device_representor&&) = default;

        auto handle() const { return handle_.handle(); }

        [[nodiscard]] static auto find_by_pci_addr(
            device const &dev,
            std::string const &pci_addr,
            doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL
        ) -> device_representor;
        
        [[nodiscard]] static auto find_by_vuid(
            device const &dev,
            std::string_view vuid,
            doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL
        ) -> device_representor;

    private:
        device_representor(doca_dev_rep *doca_handle);

        unique_handle<doca_dev_rep> handle_ { doca_dev_rep_close };
    };
}
