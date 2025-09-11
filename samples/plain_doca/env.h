#pragma once

#include <doca_build_config.h>
#include <stdlib.h>

#define DEFAULT_HOST_PCI "e3:00.0"
#define DEFAULT_DPU_PCI "03:00.0"

static inline char const *env_device_pci_address() {
    char const *env_dev = getenv("DOCA_DEV_PCI");

#ifdef DOCA_ARCH_DPU
    return env_dev ? env_dev : DEFAULT_DPU_PCI;
#else
    return env_dev ? env_dev : DEFAULT_HOST_PCI;
#endif
}

static inline char const *env_device_representor_pci_address() {
    char const *env_dev = getenv("DOCA_DEV_REP_PCI");
    return env_dev ? env_dev : DEFAULT_HOST_PCI;    
}
