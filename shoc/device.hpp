#pragma once

#include "unique_handle.hpp"
#include "error.hpp"

#include <doca_dev.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

namespace shoc {
    enum class device_capability {
        compress_deflate,
        comch_server,
        comch_client,
        dma,
        rdma,
        aes_gcm,
        sync_event_pci,
        erasure_coding
    };

    /**
     * DOCA device handle, used by contexts and memory mappings.
     */
    class device final
    {
    public:
        device() = default;

        /**
         * Underlying handle for use in DOCA SDK functions. Mainly for library-internal use.
         */
        [[nodiscard]] auto handle() const { return handle_.get(); }
        [[nodiscard]] auto as_devinfo() const -> doca_devinfo*;
        [[nodiscard]] auto has_capability(device_capability required_cap) const noexcept -> bool;
        [[nodiscard]] auto has_capabilities(std::initializer_list<device_capability> required_caps) const noexcept -> bool;

        [[nodiscard]] static auto find_by_pci_addr(std::string const &pci_addr, std::initializer_list<device_capability> required_caps = {}) -> device;
        [[nodiscard]] static auto find_by_ibdev_name(std::string const &ibdev_name, std::initializer_list<device_capability> required_caps = {}) -> device;
        [[nodiscard]] static auto find_by_capabilities(std::initializer_list<device_capability> required_caps) -> device;

        [[nodiscard]] static auto find_by_pci_addr(std::string const &pci_addr, device_capability required_cap) -> device {
            return find_by_pci_addr(pci_addr, { required_cap });
        }

        [[nodiscard]] static auto find_by_ibdev_name(std::string const &ibdev_name, device_capability required_cap) -> device {
            return find_by_ibdev_name(ibdev_name, { required_cap });
        }

        [[nodiscard]] static auto find_by_capabilities(device_capability required_cap) -> device {
            return find_by_capabilities({ required_cap });
        }

    private:
        device(doca_dev *doca_handle);

        std::shared_ptr<doca_dev> handle_;
    };

    /**
     * Device representor handle, e.g. for comch servers
     */
    class device_representor final {
    public:
        device_representor() = default;

        auto handle() const { return handle_.get(); }

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

        std::shared_ptr<doca_dev_rep> handle_;
    };
}
