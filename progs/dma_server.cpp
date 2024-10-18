#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <spdlog/fmt/bin_to_hex.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

auto handle_connection(
    doca::progress_engine *engine,
    doca::device &dev,
    doca::comch::scoped_server_connection conn
) -> doca::coro::fiber {
    auto dma = co_await engine->create_context<doca::dma_context>(dev, 16);
    auto msg = co_await conn->msg_recv();

    auto remote_desc = remote_buffer_descriptor { msg };

    auto remote_mmap = doca::memory_map { dev, remote_desc.export_desc };
    auto inv = doca::buffer_inventory { 2 };
    auto remote_buf = inv.buf_get_by_data(remote_mmap, remote_desc.src_range);

    auto local_space = std::vector<char>(remote_buf.memory().size());
    auto local_mmap = doca::memory_map { dev, local_space };
    auto local_buf = inv.buf_get_by_addr(local_mmap, local_space);

    auto status = co_await dma->memcpy(remote_buf, local_buf);

    if(status == DOCA_SUCCESS) {
        doca::logger->info("dma memcpy succeeded");

        auto copied_data = std::string_view {
            local_buf.data().data(),
            local_buf.data().size()
        };
        std::cout << "copied data: " << copied_data << std::endl;
    } else {
        doca::logger->error("dma memcpy failed: {}", doca_error_get_descr(status));
    }

    [[maybe_unused]] auto ok_status = co_await conn->send("ok");
}

auto dma_serve(
    doca::progress_engine *engine
) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("03:00.0", { doca::device_capability::dma, doca::device_capability::comch_server });
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("dma-test", dev, rep);

    for(;;) {
        auto conn = co_await server->accept();
        handle_connection(engine, dev, std::move(conn));
    }
}

auto main() -> int {
    // doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    // doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    dma_serve(&engine);

    engine.main_loop();
}