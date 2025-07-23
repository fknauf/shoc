#include "../env.hpp"

#include <shoc/shoc.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <iostream>

using stream_descriptor = boost::cobalt::use_op_t::as_default_on_t<boost::asio::posix::stream_descriptor>;

auto hotplug_device(
    shoc::progress_engine_lease engine,
    shoc::pci_address pci_addr
) -> boost::cobalt::detached try {
    auto my_executor = co_await boost::cobalt::this_coro::executor;
    auto async_stdin = stream_descriptor { my_executor };
    async_stdin.assign(STDIN_FILENO);

    shoc::logger->info("Creating PCI device type...");

    auto dev_type = shoc::devemu::pci_type { "SHOC Sample Device" };

    shoc::logger->info("Looking for suitable host device...");

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

    shoc::logger->info("started PCI device type, creating representor...");

    auto rep = dev_type.create_representor();

    shoc::logger->info("Created device representor (VUID = {}), creating emulated device context...", rep.get_vuid());

    auto emu_dev = co_await engine->create_context<shoc::devemu::pci_dev>(dev_type, rep);

    shoc::logger->info("Created device context (hotplug state = {}), hottplugging...", static_cast<int>(emu_dev->hotplug_state()));

    co_await emu_dev->hotplug();

    std::cout 
        << "hotplugged emulated device, status = " << emu_dev->hotplug_state() << "\n"
        << "press return to unplug" << std::endl;

    std::string dummy_buffer;
    co_await async_read_until(async_stdin, boost::asio::dynamic_buffer(dummy_buffer), '\n');
    co_await emu_dev->hotunplug();

    std::cout << "unplugged emulated device, status = " << emu_dev->hotplug_state() << std::endl;
} catch(shoc::doca_exception &e) {
    shoc::logger->error(e.what());
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto engine = shoc::progress_engine{};
    auto env = bluefield_env_dpu {};

    hotplug_device(
        &engine,
        env.dev_pci
    );

    co_await engine.run();
}
