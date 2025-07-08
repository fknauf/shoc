#pragma once

#include "unique_handle.hpp"
#include "error.hpp"

#include <doca_dev.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace shoc {
    enum class device_capability {
        compress_deflate,
        comch_server,
        comch_client,
        dma,
        rdma,
        aes_gcm,
        sha,
        sync_event_pci,
        erasure_coding,
        eth_rxq_cpu_cyclic,
        eth_rxq_cpu_managed_mempool,
        eth_rxq_cpu_regular,
        eth_txq_cpu_regular,
        eth_txq_l3_chksum_offload,
        eth_txq_l4_chksum_offload,
    };

    struct pci_address {
        std::string addr;
        pci_address(std::string addr): addr { std::move(addr) } {}
    };

    struct ibdev_name {
        std::string name;
        ibdev_name(std::string name): name { std::move(name) } {}
    };

    auto devinfo_matches(doca_devinfo *dev, device_capability cap) -> bool;
    auto devinfo_matches(doca_devinfo *dev, pci_address const &pci) -> bool;
    auto devinfo_matches(doca_devinfo *dev, ibdev_name const &ibdev) -> bool;
    auto devinfo_matches(doca_devinfo *dev, doca_error_t (*has_cap_fn)(doca_devinfo*)) -> bool;
    auto devinfo_matches(doca_devinfo *dev, std::invocable<bool, doca_devinfo*> auto pred_fn) -> bool {
        return pred_fn(dev);
    }

    template<typename T>
    concept device_predicate = requires(T pred, doca_devinfo *dev) {
        { devinfo_matches(dev, pred) } -> std::same_as<bool>;
    };

    class device;

    class device_list final {
    public:
        using tasks_check = auto (doca_devinfo const *) -> doca_error_t;

        device_list();
        ~device_list();

        device_list(device_list const &) = delete;
        device_list(device_list &&other);
        auto operator=(device_list const &) -> device_list & = delete;
        auto operator=(device_list &&other) -> device_list &;

        [[nodiscard]] auto begin() const { return dev_list; }
        [[nodiscard]] auto end() const { return dev_list + nb_devs; }

    private:
        auto clear() -> void;

        doca_devinfo **dev_list = nullptr;
        std::uint32_t nb_devs = 0;
    };

    class device_rep_list final {
    public:
        device_rep_list(
            device const &dev,
            doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL
        );

        ~device_rep_list();

        device_rep_list(device_rep_list const &) = delete;
        device_rep_list(device_rep_list &&other);
        auto operator=(device_rep_list const &) -> device_rep_list & = delete;
        auto operator=(device_rep_list &&other) -> device_rep_list &;

        [[nodiscard]] auto begin() const { return rep_list; }
        [[nodiscard]] auto end() const { return rep_list + nb_devs; }

    private:
        auto clear() -> void;

        doca_devinfo_rep **rep_list = nullptr;
        std::uint32_t nb_devs = 0;
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

        [[nodiscard]]
        static auto find(
            device_predicate auto &&...conditions
        ) -> device {
            for(auto dev : device_list{}) {
                if((devinfo_matches(dev, conditions) && ...)) {
                    doca_dev *dev_handle = nullptr;

                    if(DOCA_SUCCESS == doca_dev_open(dev, &dev_handle)) {
                        return device { dev_handle };
                    }
                }
            }

            throw doca_exception(DOCA_ERROR_NOT_FOUND);            
        }

        [[nodiscard]] auto get_mac_addr() const -> std::array<std::byte, DOCA_DEVINFO_MAC_ADDR_SIZE>;
        [[nodiscard]] auto get_ipv4_addr() const -> std::array<std::byte, DOCA_DEVINFO_IPV4_ADDR_SIZE>;
        [[nodiscard]] auto get_ipv6_addr() const -> std::array<std::byte, DOCA_DEVINFO_IPV6_ADDR_SIZE>;
        [[nodiscard]] auto get_pci_addr_str() const -> std::string;
        [[nodiscard]] auto get_iface_name() const -> std::string;
        [[nodiscard]] auto get_ibdev_name() const -> std::string;

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
