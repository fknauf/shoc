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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

struct test_data {
    std::uint32_t block_count;
    std::uint32_t block_size;
    std::vector<std::byte> buffer;
    std::span<std::byte> bytes;

    auto block_indices() const {
        return std::ranges::views::iota(std::uint32_t{}, block_count);
    }

    auto block(std::uint32_t i) const {
        return bytes.subspan(i * block_size, block_size);
    }

    test_data(
        std::uint32_t block_count,
        std::uint32_t block_size
    ):
        block_count { block_count },
        block_size { block_size },
        buffer(block_count * block_size + 64)
    {
        auto base_ptr = static_cast<void*>(buffer.data());
        auto space = buffer.size();
        std::align(64, block_size * block_count, base_ptr, space);

        bytes = std::span { static_cast<std::byte*>(base_ptr), block_size * block_count };

        for(auto i : block_indices()) {
            std::ranges::fill(block(i), static_cast<std::byte>(i));
        }
    }
};

auto format_extents_message(
    test_data const &data,
    doca::memory_map::export_descriptor const &export_desc
) -> std::string {
    auto msglen = 8 + export_desc.length;
    auto msg = std::string(msglen, ' ');

    std::memcpy(msg.data(), &data.block_count, 4);
    std::memcpy(msg.data() + 4, &data.block_size, 4);
    std::memcpy(msg.data() + 8, export_desc.base_ptr, export_desc.length);

    return msg;
}

auto handle_connection(
    doca::device &dev,
    test_data const &data,
    doca::comch::scoped_server_connection conn
) -> doca::coro::fiber {
    auto local_mmap = doca::memory_map { dev, data.bytes, DOCA_ACCESS_FLAG_PCI_READ_ONLY };
    auto export_desc = local_mmap.export_pci(dev);

    auto extents_msg = format_extents_message(data, export_desc);
    auto send_status = co_await conn->send(extents_msg);

    if(send_status != DOCA_SUCCESS) {
        doca::logger->error("unable to send extents: {}", doca_error_get_descr(send_status));
        co_return;
    }

    auto done_msg = co_await conn->msg_recv();

    if(done_msg == "done") {
        std::cout << "DMA transfer succeeded" << std::endl;
    } else {
        doca::logger->error("unexpected message: {}", done_msg);
    }
}

auto dma_serve(
    doca::progress_engine *engine
) -> doca::coro::fiber {
    auto data = test_data { 256, 1 << 20 };
    auto dev = doca::device::find_by_pci_addr("03:00.0", { doca::device_capability::dma, doca::device_capability::comch_server });
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("dma-test", dev, rep);

    std::cout << "accepting connections\n";

    for(;;) {
        auto conn = co_await server->accept();
        handle_connection(dev, data, std::move(conn));
    }
}

auto main() -> int {
    //doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    //doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    dma_serve(&engine);

    engine.main_loop();
}