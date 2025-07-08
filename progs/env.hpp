#pragma once

#include <shoc/device.hpp>

#include <doca_build_config.h>

#include <string>
#include <cstdlib>

namespace {
    char const *const DEFAULT_HOST_PCI = "e1:00.0";
    char const *const DEFAULT_DPU_PCI = "03:00.0";
    char const *const DEFAULT_HOST_IBDEV_NAME = "mlx5_1";
    char const *const DEFAULT_DPU_IBDEV_NAME = "mlx5_3";
}

inline auto get_envvar_with_default(char const *name, char const *default_value) {
    auto envvar = std::getenv(name);
    return envvar ? envvar : default_value;
}

struct bluefield_env_host {
    shoc::pci_address dev_pci;
    shoc::ibdev_name ibdev_name;

    bluefield_env_host():
        dev_pci { get_envvar_with_default("DOCA_DEV_PCI", DEFAULT_HOST_PCI) },
        ibdev_name { get_envvar_with_default("DOCA_IBDEV_NAME", DEFAULT_HOST_IBDEV_NAME) }
    {}
};

struct bluefield_env_dpu {
    shoc::pci_address dev_pci;
    char const *rep_pci;
    shoc::ibdev_name ibdev_name;

    bluefield_env_dpu():
        dev_pci { get_envvar_with_default("DOCA_DEV_PCI", DEFAULT_DPU_PCI) },
        rep_pci { get_envvar_with_default("DOCA_DEV_REP_PCI", DEFAULT_HOST_PCI) },
        ibdev_name { get_envvar_with_default("DOCA_IBDEV_NAME", DEFAULT_DPU_IBDEV_NAME) }
    {}
};

#ifdef DOCA_ARCH_DPU
using bluefield_env = bluefield_env_dpu;
#else
using bluefield_env = bluefield_env_host;
#endif
