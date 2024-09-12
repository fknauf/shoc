#pragma once

#include "device.hpp"

namespace doca {
    class comch_device:
        public device
    {
    public:
        comch_device(std::string const &pci_addr);
    };
}
