#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/device.hpp>
#include <doca/progress_engine.hpp>
#include <doca/rdma.hpp>

#include <iostream>
#include <string_view>
#include <vector>

auto rdma_exchange_connection_details(
    doca::progress_engine *engine,
    std::span<std::byte const> local_conn_details
) -> doca::coro::eager_task<std::string> {
    auto dev = doca::device::find_by_pci_addr("81:00.0", doca::device_capability::comch_server);
    auto client = co_await engine->create_context<doca::comch::client>("vss-rdma-oob-send-receive-test", dev);
    
    auto err = co_await client->send(local_conn_details);

    if(err != DOCA_SUCCESS) {
        throw doca::doca_exception(err);
    }
   
    co_return co_await client->msg_recv();
}

auto rdma_send(
    doca::progress_engine *engine
) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("81:00.0", doca::device_capability::rdma);

    auto rdma = co_await engine->create_context<doca::rdma_context>(dev);
    auto conn_details = rdma->oob_export();

    auto remote_conn_details_as_string = co_await rdma_exchange_connection_details(engine, conn_details);
    auto remote_conn_details = std::span { 
        reinterpret_cast<std::byte const*>(remote_conn_details_as_string.data()), 
        remote_conn_details_as_string.size()
    };

    auto err = rdma->oob_connect(remote_conn_details);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("could not connect to remote RDMA context: {}", doca_error_get_descr(err));
        co_return;
    }

    auto data = std::string { "Hello, bRainDMAged." };
    auto mmap = doca::memory_map { dev, data };
    auto bufinv = doca::buffer_inventory { 1 };
    auto send_buf = bufinv.buf_get_by_data(mmap, data);

    err = co_await rdma->send(send_buf, 42);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("failed to send data: {}", doca_error_get_descr(err));
    }
}

auto main() -> int {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine {};

    rdma_send(&engine);

    engine.main_loop();
}
