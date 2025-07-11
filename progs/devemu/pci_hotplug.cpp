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

    auto dev_type = shoc::devemu::pci_type { "SHOC Sample Device" };

    auto phys_dev = shoc::device::find(
        pci_addr,
        dev_type.hotplug_device_predicate()
    );

    auto err = dev_type
        .set_dev(phys_dev)
        .set_device_id(0x1021)
        .set_vendor_id(0x15b3)
        .set_subsystem_id(0x0051)
        .set_subsystem_vendor_id(0x15b3)
        .set_revision_id(0)
        .set_class_code(0x020000)
        .set_num_msix(0)
        .start();

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("could not start PCI device type: {}", doca_error_get_descr(err));
        co_return;
    }

    auto rep = dev_type.create_representor();
    auto emu_dev = co_await engine->create_context<shoc::devemu::pci_dev>(dev_type, rep);

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
    auto engine = shoc::progress_engine{};
    auto env = bluefield_env_dpu {};

    hotplug_device(
        &engine,
        env.dev_pci
    );

    co_await engine.run();
}
