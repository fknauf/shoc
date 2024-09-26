#pragma once

#include <doca/device.hpp>

namespace doca::comch {
    class comch_device:
        public device
    {
    public:
        comch_device(std::string const &pci_addr);
    };
}
