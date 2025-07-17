#include "../../env.hpp"

#include <shoc/shoc.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <cxxopts.hpp>

auto devemu_dma_demo_host(
    [[maybe_unused]] shoc::progress_engine_lease engine,
    [[maybe_unused]] std::string pci_address,
    [[maybe_unused]] int vfio_group,
    [[maybe_unused]] std::string write_data
) -> boost::cobalt::detached {
    co_return;
}

auto co_main(
    int argc,
    char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_host {};
    auto options = cxxopts::Options("shoc-devemu-pci-dma", "PCI device emulation: DMA demo program");

    options.add_options()
        (
            "p,pci-addr",
            "PCI address of the emulated device",
            cxxopts::value<std::string>()->default_value(env.dev_pci.addr)
        )
        (
            "g.vfio-group",
            "VFIO group of the device. Integer",
            cxxopts::value<int>()->default_value("-1")
        )
        (
            "w,write-data",
            "Data to write to the host memory",
            cxxopts::value<std::string>()->default_value("This is a sample piece of data from DPU!")
        )
        ;

    auto cmdline = options.parse(argc, argv);

    auto pci_addr = cmdline["pci-addr"].as<std::string>();
    auto vfio_group = cmdline["vfio-group"].as<int>();
    auto write_data = cmdline["write-data"].as<std::string>();

    auto engine = shoc::progress_engine{};

    devemu_dma_demo_host(
        &engine,
        pci_addr,
        vfio_group,
        write_data
    );

    co_await engine.run();
}
