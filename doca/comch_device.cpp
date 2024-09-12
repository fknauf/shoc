#include "comch_device.hpp"
#include "logger.hpp"

#include <string_view>

namespace doca {
    comch_device::comch_device(std::string const &pci_addr):
        device(device::find_by_pci_addr(pci_addr))
    {
    }
}
