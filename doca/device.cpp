#include "device.hpp"

#include <doca_compress.h>

#include <cstdint>
#include <memory>
#include <span>

namespace doca {
    namespace {
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
    }

    device::device(doca_dev *doca_handle)
    {
        if(doca_handle == nullptr) {
            throw doca_exception(DOCA_ERROR_NOT_FOUND);
        }

        handle_.reset(doca_handle);
    }

    auto device::as_devinfo() const -> doca_devinfo* {
        return doca_dev_as_devinfo(handle());
    }

    auto device::find_by_pci_addr(std::string const &pci_addr, tasks_check *check_fun) -> device {
        for(auto dev : device_list()) {
            std::uint8_t is_addr_equal = 0;
            auto err = doca_devinfo_is_equal_pci_addr(dev, pci_addr.c_str(), &is_addr_equal);

            if(err == DOCA_SUCCESS && is_addr_equal) {
                if(check_fun != nullptr && DOCA_SUCCESS != check_fun(dev)) {
                    continue;
                }

                doca_dev *dev_handle = nullptr;
                
                if(DOCA_SUCCESS == doca_dev_open(dev, &dev_handle)) {
                    return device { dev_handle };
                }
            }
        }

        throw doca_exception(DOCA_ERROR_NOT_FOUND);
    }

    auto device::find_by_capabilities(tasks_check *check_fun) -> device {
        for(auto dev : device_list()) {
            if(DOCA_SUCCESS != check_fun(dev)) {
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
        handle_.reset(doca_handle);
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
