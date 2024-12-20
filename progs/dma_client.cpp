#include "dma_common.hpp"

#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <spdlog/fmt/bin_to_hex.h>

#include <span>
#include <string_view>
#include <vector>

auto dma_send(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("81:00.0", { doca::device_capability::comch_client, doca::device_capability::dma });

    auto client = co_await engine->create_context<doca::comch::client>("dma-test", dev);

    auto mem = std::vector<char>(1 << 20);
    auto mmap = doca::memory_map { dev, mem, DOCA_ACCESS_FLAG_PCI_READ_ONLY };

    auto export_desc = mmap.export_pci(dev);
    auto remote_desc = remote_buffer_descriptor { mem, export_desc };

    co_await client->send(remote_desc.format());

    auto response = co_await client->msg_recv();

    if(response == "done") {
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
