#include "../../env.hpp"

#include <shoc/shoc.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>
#include <cxxopts.hpp>

#include <iostream>

using stream_descriptor = boost::cobalt::use_op_t::as_default_on_t<boost::asio::posix::stream_descriptor>;

auto devemu_dma_demo(
    shoc::progress_engine_lease engine,
    shoc::pci_address pci_addr,
    std::string vuid,
    std::span<std::byte> remote_iova,
    std::string write_data
) -> boost::cobalt::detached try {
    shoc::logger->info("Creating PCI device type...");

    auto dev_type = shoc::devemu::pci_type { "SHOC Sample Device" };

    shoc::logger->info("Looking for suitable host device (PCI {})...", pci_addr.addr);

    auto phys_dev = shoc::device::find(
        pci_addr,
        dev_type.hotplug_device_predicate()
    );

    shoc::logger->info("Found host device, configuring and starting PCI type...");

    auto err = dev_type
        .set_dev(phys_dev)
        .set_device_id(0x1021)
        .set_vendor_id(0x15b3)
        .set_subsystem_id(0x0051)
        .set_subsystem_vendor_id(0x15b3)
        .set_revision_id(0)
        .set_class_code(0x020000)
        .set_num_msix(4)
        // 64-bit bar requires two slots to configure
        .set_memory_bar_conf(0, 0xe, DOCA_DEVEMU_PCI_BAR_MEM_TYPE_64_BIT, true)
        .set_memory_bar_conf(1, 0x0, DOCA_DEVEMU_PCI_BAR_MEM_TYPE_64_BIT, false)
        .set_bar_db_region_by_offset_conf(0, 0x0, 0x1000, 0x2, 0x2)
        .set_bar_msix_table_region_conf(0, 0x1000, 0x1000)
        .set_bar_msix_pba_region_conf(0, 0x2000, 0x1000)
        .set_bar_stateful_region_conf(0, 0x3000, 0x100)
        .start();

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("could not start PCI device type: {}", doca_error_get_descr(err));
        co_return;
    }

    shoc::logger->info("started PCI device type, finding representor (VUID = {})...", vuid);

    auto rep = vuid == ""
        ? dev_type.create_representor()
        : shoc::device_representor::find_by_vuid(phys_dev, vuid, DOCA_DEVINFO_REP_FILTER_EMULATED);

    shoc::logger->info("Found device representor (VUID = {}), creating emulated device context...", rep.get_vuid());

    auto emu_dev = co_await engine->create_context<shoc::devemu::pci_dev>(dev_type, rep);

    shoc::logger->info("Created device context (hotplug state = {}), setting up DMA context...", static_cast<int>(emu_dev->hotplug_state()));

    auto dma = co_await engine->create_context<shoc::dma_context>(phys_dev, 1);

    shoc::logger->info("Created DMA context");

    auto remote_mmap = emu_dev->remote_mmap(phys_dev, remote_iova);
    auto local_memory = std::string(remote_iova.size(), ' ');
    auto local_mmap = shoc::memory_map { phys_dev, local_memory };
    auto buf_inv = shoc::buffer_inventory { 2 };

    auto remote_buf = buf_inv.buf_get_by_data(remote_mmap, remote_iova);
    auto local_buf = buf_inv.buf_get_by_addr(local_mmap, local_memory);

    auto send_status = co_await dma->memcpy(remote_buf, local_buf);

    if(send_status != DOCA_SUCCESS) {
        shoc::logger->error("DMA memcpy host -> dpu failed: {}", doca_error_get_descr(send_status));
        co_return;
    }

    if(write_data == "") {
        co_return;
    }

    local_memory = write_data;
    send_status = co_await dma->memcpy(local_buf, remote_buf);

    if(send_status != DOCA_SUCCESS) {
        shoc::logger->error("DMA memcpy dpu -> host failed: {}", doca_error_get_descr(send_status));
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->error(e.what());
}

auto co_main(
    int argc,
    char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_dpu {};
    auto options = cxxopts::Options("shoc-devemu-pci-dma", "PCI device emulation: DMA demo program");

    options.add_options()
        (
            "h,help",
            "Print Usage"
        )
        (
            "p,pci-addr",
            "Host Device PCI address",
            cxxopts::value<std::string>()->default_value(env.dev_pci.addr)
        )
        (
            "u,vuid",
            "Emulated Device VUID",
            cxxopts::value<std::string>()->default_value("")
        )
        (
            "a,addr",
            "Host DMA memory IOVA address",
            cxxopts::value<std::uint64_t>()->default_value("0x1000000")
        )
        (
            "w,write-data",
            "Data to write to the host memory",
            cxxopts::value<std::string>()->default_value("This is a sample piece of data from DPU!")
        )
        ;

    auto cmdline = options.parse(argc, argv);

    if(cmdline.count("help") > 0) {
        std::cout << options.help() << std::endl;
        co_return 0;
    }

    auto vuid = cmdline["vuid"].as<std::string>();
    auto pci_addr = cmdline["pci-addr"].as<std::string>();
    auto remote_iova_baseptr = reinterpret_cast<std::byte*>(cmdline["addr"].as<std::uint64_t>());
    auto remote_iova = std::span { remote_iova_baseptr, 4096 };
    auto write_data = cmdline["write-data"].as<std::string>();

    auto engine = shoc::progress_engine{};

    std::cout << pci_addr << std::endl;

    devemu_dma_demo(
        &engine,
        pci_addr,
        vuid,
        remote_iova,
        write_data
    );

    co_await engine.run();
}
