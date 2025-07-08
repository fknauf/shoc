#include "device.hpp"

#include "common/overload.hpp"

#include <doca_aes_gcm.h>
#include <doca_compress.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_dma.h>
#include <doca_erasure_coding.h>
#include <doca_eth_rxq.h>
#include <doca_eth_txq.h>
#include <doca_rdma.h>
#include <doca_sha.h>
#include <doca_sync_event.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>

namespace shoc {
    namespace {
        auto dev_cleanup(doca_dev *handle) noexcept -> void {
            if(handle != nullptr) {
                doca_dev_close(handle);
            }
        }

        auto dev_rep_cleanup(doca_dev_rep *handle) noexcept -> void {
            if(handle != nullptr) {
                doca_dev_rep_close(handle);
            }
        }

        auto devinfo_has_capability(
            doca_devinfo *dev,
            device_capability required_cap
        ) -> bool {
            switch(required_cap) {
                case device_capability::compress_deflate:
                    return doca_compress_cap_task_compress_deflate_is_supported(dev) == DOCA_SUCCESS
                        && doca_compress_cap_task_decompress_deflate_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::comch_client:
                    return doca_comch_cap_client_is_supported(dev) == DOCA_SUCCESS
                        && doca_comch_consumer_cap_is_supported(dev) == DOCA_SUCCESS
                        && doca_comch_producer_cap_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::comch_server:
                    return doca_comch_cap_server_is_supported(dev) == DOCA_SUCCESS
                        && doca_comch_consumer_cap_is_supported(dev) == DOCA_SUCCESS
                        && doca_comch_producer_cap_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::dma:
                    return doca_dma_cap_task_memcpy_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::rdma:
                    return doca_rdma_cap_task_receive_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_send_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_send_imm_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_read_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_write_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_write_imm_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_atomic_cmp_swp_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_atomic_fetch_add_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_remote_net_sync_event_get_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_remote_net_sync_event_notify_set_is_supported(dev) == DOCA_SUCCESS
                        && doca_rdma_cap_task_remote_net_sync_event_notify_add_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::aes_gcm:
                    return doca_aes_gcm_cap_task_encrypt_is_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_encrypt_is_tag_96_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_encrypt_is_tag_128_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_encrypt_is_key_type_supported(dev, DOCA_AES_GCM_KEY_128) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_encrypt_is_key_type_supported(dev, DOCA_AES_GCM_KEY_256) == DOCA_SUCCESS
                        && doca_aes_gcm_cap_task_decrypt_is_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_decrypt_is_tag_96_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_decrypt_is_tag_128_supported(dev) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_decrypt_is_key_type_supported(dev, DOCA_AES_GCM_KEY_128) == DOCA_SUCCESS
                        //&& doca_aes_gcm_cap_task_decrypt_is_key_type_supported(dev, DOCA_AES_GCM_KEY_256) == DOCA_SUCCESS
                        ;
                case device_capability::sha:
                    return doca_sha_cap_task_hash_get_supported(dev, DOCA_SHA_ALGORITHM_SHA256) == DOCA_SUCCESS
                        && doca_sha_cap_task_partial_hash_get_supported(dev, DOCA_SHA_ALGORITHM_SHA256) == DOCA_SUCCESS
                        ;
                case device_capability::sync_event_pci:
                    return doca_sync_event_cap_is_export_to_remote_pci_supported(dev) == DOCA_SUCCESS
                        && doca_sync_event_cap_task_wait_eq_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::erasure_coding:
                    return doca_ec_cap_task_create_is_supported(dev) == DOCA_SUCCESS
                        && doca_ec_cap_task_update_is_supported(dev) == DOCA_SUCCESS
                        && doca_ec_cap_task_recover_is_supported(dev) == DOCA_SUCCESS
                        ;
                case device_capability::eth_rxq_cpu_cyclic:
                    return doca_eth_rxq_cap_is_type_supported(dev, DOCA_ETH_RXQ_TYPE_CYCLIC, DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU) == DOCA_SUCCESS;
                case device_capability::eth_rxq_cpu_managed_mempool:
                    return doca_eth_rxq_cap_is_type_supported(dev, DOCA_ETH_RXQ_TYPE_MANAGED_MEMPOOL, DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU) == DOCA_SUCCESS;
                case device_capability::eth_rxq_cpu_regular:
                    return doca_eth_rxq_cap_is_type_supported(dev, DOCA_ETH_RXQ_TYPE_REGULAR, DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU) == DOCA_SUCCESS;
                case device_capability::eth_txq_cpu_regular:
                    return doca_eth_txq_cap_is_type_supported(dev, DOCA_ETH_TXQ_TYPE_REGULAR, DOCA_ETH_TXQ_DATA_PATH_TYPE_CPU) == DOCA_SUCCESS;
                case device_capability::eth_txq_l3_chksum_offload:
                    return doca_eth_txq_cap_is_l3_chksum_offload_supported(dev) == DOCA_SUCCESS;
                case device_capability::eth_txq_l4_chksum_offload:
                    return doca_eth_txq_cap_is_l4_chksum_offload_supported(dev) == DOCA_SUCCESS;
                default:
                    return false;
            }
        }

        auto devinfo_has_capabilities(doca_devinfo *dev, std::initializer_list<device_capability> required_caps) -> bool {
            return std::ranges::all_of(
                required_caps,
                [dev](device_capability cap) {
                    return devinfo_has_capability(dev, cap);
                }
            );
        }
    }

    auto devinfo_matches(
        doca_devinfo *dev,
        device_capability cap
    ) -> bool {
        return devinfo_has_capability(dev, cap);
    }

    auto devinfo_matches(
        doca_devinfo *dev,
        pci_address const &pci
    ) -> bool {
        std::uint8_t is_addr_equal = 0;
        auto err = doca_devinfo_is_equal_pci_addr(dev, pci.addr.c_str(), &is_addr_equal);
        return err == DOCA_SUCCESS && is_addr_equal;
    }

    auto devinfo_matches(
        doca_devinfo *dev,
        ibdev_name const &ibdev
    ) -> bool {
        char dev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];
        auto err = doca_devinfo_get_ibdev_name(dev, dev_name, sizeof dev_name);
        return err == DOCA_SUCCESS && dev_name == ibdev.name;
    }

    auto devinfo_matches(
        doca_devinfo *dev,
        doca_error_t (*has_cap_fn)(doca_devinfo *)
    ) -> bool {
        return has_cap_fn(dev) == DOCA_SUCCESS;
    }

    device_list::device_list() {
        enforce_success(doca_devinfo_create_list(&dev_list, &nb_devs));
    }

    device_list::~device_list() {
        clear();
    }

    device_list::device_list(device_list &&other) {
        *this = std::move(other);
    }

    auto device_list::operator=(device_list &&other) -> device_list& {
        clear();
        dev_list = std::exchange(other.dev_list, nullptr);
        nb_devs = std::exchange(other.nb_devs, 0);
        return *this;
    }

    auto device_list::clear() -> void {
        if(dev_list != nullptr) {
            doca_devinfo_destroy_list(dev_list);
            dev_list = nullptr;
            nb_devs = 0;
        }
    }

    device_rep_list::device_rep_list(
        device const &dev,
        doca_devinfo_rep_filter filter
    ) {
        enforce_success(doca_devinfo_rep_create_list(dev.handle(), filter, &rep_list, &nb_devs));
    }

    device_rep_list::~device_rep_list() {
        clear();
    }

    device_rep_list::device_rep_list(device_rep_list &&other) { 
        *this = std::move(other);
    }

    auto device_rep_list::operator=(device_rep_list &&other) -> device_rep_list& {
        clear();
        rep_list = std::exchange(other.rep_list, nullptr);
        nb_devs = std::exchange(other.nb_devs, 0);
        return *this;
    } 

    auto device_rep_list::clear() -> void {
        if(rep_list != nullptr) {
            doca_devinfo_rep_destroy_list(rep_list);
            rep_list = nullptr;
        }
    }

    device::device(doca_dev *doca_handle)
    {
        if(doca_handle == nullptr) {
            throw doca_exception(DOCA_ERROR_NOT_FOUND);
        }

        handle_.reset(doca_handle, dev_cleanup);
    }

    auto device::as_devinfo() const -> doca_devinfo* {
        return doca_dev_as_devinfo(handle());
    }

    auto device::has_capability(device_capability required_cap) const noexcept -> bool {
        return devinfo_has_capability(as_devinfo(), required_cap);
    }

    auto device::has_capabilities(std::initializer_list<device_capability> required_caps) const noexcept -> bool {
        return devinfo_has_capabilities(as_devinfo(), required_caps);
    }

    device_representor::device_representor(doca_dev_rep *doca_handle) {
        handle_.reset(doca_handle, dev_rep_cleanup);
    }

    auto device_representor::find_by_pci_addr(
        device const &dev,
        std::string const &pci_addr,
        doca_devinfo_rep_filter filter
    ) -> device_representor
    {
        std::uint8_t is_addr_equal;
        doca_dev_rep *result;

        for(auto rep : device_rep_list(dev, filter)) {
            auto err = doca_devinfo_rep_is_equal_pci_addr(rep, pci_addr.c_str(), &is_addr_equal);

            if(err == DOCA_SUCCESS && is_addr_equal) {
                enforce_success(doca_dev_rep_open(rep, &result));
                return device_representor { result };
            }
        }

        throw doca_exception(DOCA_ERROR_NOT_FOUND);
    }

    auto device_representor::find_by_vuid(
        device const &dev,
        std::string_view vuid,
        doca_devinfo_rep_filter filter
    ) -> device_representor
    {
        char vuid_buf[DOCA_DEVINFO_VUID_SIZE + 1] = "";
        doca_dev_rep *result;

        for(auto rep : device_rep_list(dev, filter)) {
            auto err = doca_devinfo_rep_get_vuid(rep, vuid_buf, DOCA_DEVINFO_REP_VUID_SIZE);

            if(err != DOCA_SUCCESS) {
                continue;
            }

            if(vuid == vuid_buf) {
                enforce_success(doca_dev_rep_open(rep, &result));
                return device_representor { result };
            }
        }

        throw doca_exception(DOCA_ERROR_NOT_FOUND);
    }

    auto device::get_mac_addr() const -> std::array<std::byte, DOCA_DEVINFO_MAC_ADDR_SIZE> {
        std::array<std::byte, DOCA_DEVINFO_MAC_ADDR_SIZE> result;
        enforce_success(doca_devinfo_get_mac_addr(
            as_devinfo(),
            reinterpret_cast<std::uint8_t*>(result.data()),
            result.size()
        ));
        return result;
    }

    auto device::get_ipv4_addr() const -> std::array<std::byte, DOCA_DEVINFO_IPV4_ADDR_SIZE> {
        std::array<std::byte, DOCA_DEVINFO_IPV4_ADDR_SIZE> result;
        enforce_success(doca_devinfo_get_ipv4_addr(
            as_devinfo(),
            reinterpret_cast<std::uint8_t*>(result.data()),
            result.size()
        ));
        return result;
    }

    auto device::get_ipv6_addr() const -> std::array<std::byte, DOCA_DEVINFO_IPV6_ADDR_SIZE> {
        std::array<std::byte, DOCA_DEVINFO_IPV6_ADDR_SIZE> result;
        enforce_success(doca_devinfo_get_ipv6_addr(
            as_devinfo(),
            reinterpret_cast<std::uint8_t*>(result.data()),
            result.size()
        ));
        return result;
    }

    auto device::get_pci_addr_str() const -> std::string {
        char buf[DOCA_DEVINFO_PCI_ADDR_SIZE];
        enforce_success(doca_devinfo_get_pci_addr_str(
            as_devinfo(),
            buf
        ));
        return buf;
    }

    auto device::get_iface_name() const -> std::string {
        char buf[DOCA_DEVINFO_IFACE_NAME_SIZE];
        enforce_success(doca_devinfo_get_iface_name(
            as_devinfo(),
            buf,
            sizeof buf
        ));
        return buf;
    }

    auto device::get_ibdev_name() const -> std::string {
        char buf[DOCA_DEVINFO_IBDEV_NAME_SIZE];
        enforce_success(doca_devinfo_get_ibdev_name(
            as_devinfo(),
            buf,
            sizeof buf
        ));
        return buf;
    }
}
