#include "env.hpp"

#include <shoc/aligned_memory.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/device.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/rdma.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <iostream>
#include <string_view>
#include <vector>

#include <fmt/printf.h>


auto rdma_cm_serve(
    shoc::progress_engine_lease engine,
    char const *dev_pci
) -> boost::cobalt::detached try {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::rdma);
    auto rdma = co_await engine->create_context<shoc::rdma_context>(dev);

    shoc::logger->debug("listening for RDMA CM on port 18515...");

    auto conn = co_await rdma->listen(18515);

    shoc::logger->debug("connected.");

    auto membuffer = shoc::aligned_memory { 1 << 23 };
    auto space = membuffer.as_writable_bytes();
    auto mmap = shoc::memory_map { dev, space };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto recv_buf = bufinv.buf_get_by_addr(mmap, space);

    std::uint32_t immediate_data = 0;

    shoc::logger->debug("receiving data...");

    auto err = co_await conn.receive(recv_buf, &immediate_data);

    shoc::logger->debug("data received.");

    if(err == DOCA_SUCCESS) {
        fmt::print("{}\nimm = {}\n", std::string_view{ recv_buf.data().begin(), recv_buf.data().end() }, immediate_data);
    } else {
        shoc::logger->error("failed to receive data: {}", doca_error_get_descr(err));
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->error("SHOC error: {}", e.what());
} catch(boost::system::error_code &e) {
    shoc::logger->error("Boost Error: {}", e.what());
} catch(std::exception &e) {
    shoc::logger->error("Generic Error: {}", e.what());
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env{};
    auto engine = shoc::progress_engine{};

    rdma_cm_serve(&engine, env.dev_pci);

    co_await engine.run();
}
