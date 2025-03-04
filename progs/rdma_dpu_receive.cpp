#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/device.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/rdma.hpp>

#include <boost/cobalt.hpp>

#include <iostream>
#include <string_view>
#include <vector>

#include <fmt/printf.h>

auto rdma_exchange_connection_details(
    shoc::progress_engine_lease engine,
    std::span<std::byte const> local_conn_details,
    char const *dev_pci,
    char const *rep_pci
) -> boost::cobalt::promise<std::vector<std::byte>> {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::comch_server);
    auto rep = shoc::device_representor::find_by_pci_addr(dev, rep_pci, DOCA_DEVINFO_REP_FILTER_NET);
    auto server = co_await engine->create_context<shoc::comch::server>("shoc-rdma-oob-send-receive-test", dev, rep);

    auto conn = co_await server->accept();
    auto remote_msg = co_await conn->msg_recv();
    auto err = co_await conn->send(local_conn_details);

    if(err != DOCA_SUCCESS) {
        throw shoc::doca_exception(err);
    }

    auto bytes = std::vector<std::byte>(
        reinterpret_cast<std::byte*>(remote_msg.data()),
        reinterpret_cast<std::byte*>(remote_msg.data()) + remote_msg.size()
    );

    co_return bytes;
}

auto rdma_receive(
    shoc::progress_engine_lease engine,
    char const *dev_pci,
    char const *rep_pci
) -> boost::cobalt::detached {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::rdma);
    auto rdma = co_await engine->create_context<shoc::rdma_context>(dev);
    auto conn = rdma->export_connection();

    auto remote_conn_details = co_await rdma_exchange_connection_details(engine, conn.details(), dev_pci, rep_pci);
    conn.connect(remote_conn_details);

    auto space = std::vector<char>(1024);
    auto mmap = shoc::memory_map { dev, space };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto recv_buf = bufinv.buf_get_by_addr(mmap, space);

    std::uint32_t immediate_data = 0;
    auto err = co_await rdma->receive(recv_buf, &immediate_data);

    if(err == DOCA_SUCCESS) {
        fmt::printf("{}\nimm = {}", std::string_view{ recv_buf.data().begin(), recv_buf.data().end() }, immediate_data);
    } else {
        shoc::logger->error("failed to receive data: {}", doca_error_get_descr(err));
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_dpu{};
    auto engine = shoc::progress_engine{};

    rdma_receive(&engine, env.dev_pci, env.rep_pci);

    co_await engine.run();
}
