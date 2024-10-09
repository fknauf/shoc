#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <span>
#include <string_view>
#include <vector>

auto dma_send(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("81:00.0", doca::device_capability::comch_client);

    auto client = co_await engine->create_context<doca::comch::client>("dma-test", dev);

    auto mem = std::vector<char>(1024, 'd');
    auto mmap = doca::memory_map { dev, mem };
    auto inv = doca::buffer_inventory { 1 };
    auto buf = inv.buf_get_by_data(mmap, mem);

    auto export_desc = mmap.export_pci(dev);
    auto remote_desc = remote_buffer_descriptor { mem, export_desc };

    co_await client->send(remote_desc.format());

    auto response = co_await client->msg_recv();

    if(response == "ok") {
        doca::logger->info("DMA transfer succeeded");
    } else {
        doca::logger->error("unexpected response message: {}", response);
    }
}

auto main() -> int {
    auto engine = doca::progress_engine{};

    dma_send(&engine);

    engine.main_loop();
}
