#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/device.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/rdma.hpp>

#include <iostream>
#include <string_view>
#include <vector>

auto rdma_exchange_connection_details(
    shoc::progress_engine *engine,
    std::span<std::byte const> local_conn_details,
    char const *dev_pci
) -> shoc::coro::eager_task<std::string> {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::comch_client);
    auto client = co_await engine->create_context<shoc::comch::client>("shoc-rdma-oob-send-receive-test", dev);
    
    auto err = co_await client->send(local_conn_details);

    if(err != DOCA_SUCCESS) {
        throw shoc::doca_exception(err);
    }
   
    co_return co_await client->msg_recv();
}

auto rdma_send(
    shoc::progress_engine *engine,
    char const *dev_pci
) -> shoc::coro::fiber {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::rdma);

    auto rdma = co_await engine->create_context<shoc::rdma_context>(dev);
    auto conn = rdma->export_connection();

    auto remote_conn_details = co_await rdma_exchange_connection_details(engine, conn.details(), dev_pci);
    conn.connect(remote_conn_details);

    auto data = std::string { "Hello, bRainDMAged." };
    auto mmap = shoc::memory_map { dev, data };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto send_buf = bufinv.buf_get_by_data(mmap, data);

    auto err = co_await conn.send(send_buf, 42);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failed to send data: {}", doca_error_get_descr(err));
    }
}

auto main() -> int {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_host{};
    auto engine = shoc::progress_engine {};

    rdma_send(&engine, env.dev_pci);

    engine.main_loop();
}
