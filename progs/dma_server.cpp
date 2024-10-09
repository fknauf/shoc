#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

auto dma_serve(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("03:00.0", { doca::device_capability::dma, doca::device_capability::comch_server });
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("dma-test", dev, rep);
    auto dma = co_await engine->create_context<doca::dma_context>(dev, 16);

    auto conn = co_await server->accept();
    auto msg = co_await conn->msg_recv();
    auto remote_buffer_desc = remote_buffer_descriptor { msg };

    auto remote_mmap = doca::memory_map { dev, remote_buffer_desc.export_desc };
    auto inv = doca::buffer_inventory { 2 };
    auto remote_buf = inv.buf_get_by_addr(remote_mmap, remote_buffer_desc.src_range);

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
        doca::logger->error("dma memcpy failed");
    }

    [[maybe_unused]] auto ok_status = co_await conn->send("ok");
}

auto main() -> int {
    auto engine = doca::progress_engine{};

    dma_serve(&engine);

    engine.main_loop();
}