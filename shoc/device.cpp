#include "device.hpp"

#include <doca_aes_gcm.h>
#include <doca_compress.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_dma.h>
#include <doca_rdma.h>
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

        class device_list {
        public:
            using tasks_check = auto (doca_devinfo const *) -> doca_error_t;

            device_list() {
                enforce_success(doca_devinfo_create_list(&dev_list, &nb_devs));
            }

            ~device_list() {
                if(dev_list != nullptr) {
                    doca_devinfo_destroy_list(dev_list);
                }
            }

            device_list(device_list const &) = delete;
            device_list &operator=(device_list const &) = delete;

            [[nodiscard]] auto begin() const { return dev_list; }
            [[nodiscard]] auto end() const { return dev_list + nb_devs; }

        private:
            doca_devinfo **dev_list = nullptr;
            std::uint32_t nb_devs = 0;
        };

        class device_rep_list {
        public:
            device_rep_list(device const &dev, doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL) {
                enforce_success(doca_devinfo_rep_create_list(dev.handle(), filter, &rep_list, &nb_devs));
            }

            ~device_rep_list() {
                if(rep_list != nullptr) {
                    doca_devinfo_rep_destroy_list(rep_list);
                }
            }

            device_rep_list(device_rep_list const &) = delete;
            device_rep_list &operator=(device_rep_list const &) = delete;

            [[nodiscard]] auto begin() const { return rep_list; }
            [[nodiscard]] auto end() const { return rep_list + nb_devs; }

        private:
            doca_devinfo_rep **rep_list = nullptr;
            std::uint32_t nb_devs = 0;
        };

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
                case device_capability::sync_event_pci:
                    return doca_sync_event_cap_is_export_to_remote_pci_supported(dev) == DOCA_SUCCESS
                        && doca_sync_event_cap_task_wait_eq_is_supported(dev) == DOCA_SUCCESS
                        ;
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

    auto device::find_by_pci_addr(std::string const &pci_addr, std::initializer_list<device_capability> required_caps) -> device {
        for(auto dev : device_list{}) {
            std::uint8_t is_addr_equal = 0;
            auto err = doca_devinfo_is_equal_pci_addr(dev, pci_addr.c_str(), &is_addr_equal);

            if(
                err == DOCA_SUCCESS &&
                is_addr_equal &&
                devinfo_has_capabilities(dev, required_caps)
            ) {
                doca_dev *dev_handle = nullptr;

                if(DOCA_SUCCESS == doca_dev_open(dev, &dev_handle)) {
                    return device { dev_handle };
                }
            }
        }

        throw doca_exception(DOCA_ERROR_NOT_FOUND);
    }

    auto device::find_by_capabilities(std::initializer_list<device_capability> required_caps) -> device {
        for(auto dev : device_list()) {
            if(!devinfo_has_capabilities(dev, required_caps)) {
                continue;
            }

            doca_dev *dev_handle = nullptr;

            if(DOCA_SUCCESS == doca_dev_open(dev, &dev_handle)) {
                return device { dev_handle };
            }
        }

        throw doca_exception(DOCA_ERROR_NOT_FOUND);
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

}
