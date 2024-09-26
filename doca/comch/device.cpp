#include "device.hpp"

#include <doca/logger.hpp>

namespace doca::comch {
    comch_device::comch_device(std::string const &pci_addr):
        device(device::find_by_pci_addr(pci_addr))
    {
    }
}
