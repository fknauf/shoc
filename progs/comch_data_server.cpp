#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/producer.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/logger.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <iostream>
#include <memory>
#include <ranges>
#include <string_view>
#include <vector>

struct data_descriptor {
    std::uint32_t block_count;
    std::uint32_t block_size;
    std::vector<std::byte> buffer;
    std::span<std::byte> bytes;

    auto block(std::uint32_t i) const {
        return bytes.subspan(i * block_size, block_size);
    }

    data_descriptor(
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

        for(auto i : std::ranges::views::iota(std::uint32_t{}, block_count)) {
            std::ranges::fill(block(i), static_cast<std::byte>(i));
        }
    }
};

auto send_blocks(
    doca::comch::scoped_server_connection con,
    data_descriptor &data,
    doca::memory_map &mmap,
    doca::buffer_inventory &bufinv
) -> doca::coro::fiber {
    auto send_status = co_await con->send(fmt::format("{} {}", data.block_count, data.block_size));

    if(send_status != DOCA_SUCCESS) {
        doca::logger->error("failed to send data gemoetry");
        co_return;
    }

    auto consumer_id = co_await con->accept_consumer();
    auto prod = co_await con->create_producer(16);

    for(auto i : std::ranges::views::iota(std::uint32_t{}, data.block_count)) {
        auto buffer = bufinv.buf_get_by_data(mmap, data.block(i));
        auto status = co_await prod->send(buffer, {}, consumer_id);

        if(status != DOCA_SUCCESS) {
            doca::logger->error("producer failed to send buffer: {}", doca_error_get_descr(status));
            co_return;
        }
    }
}

auto serve(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("03:00.0", doca::device_capability::comch_server);
    auto rep = doca::device_representor::find_by_pci_addr(dev, "81:00.0", DOCA_DEVINFO_REP_FILTER_NET);
    auto data = data_descriptor { 256, 2 << 20 };
    auto mmap = doca::memory_map { dev, data.bytes, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = doca::buffer_inventory { 32 };

    auto server = co_await engine->create_context<doca::comch::server>("vss-data-test", dev, rep);

    std::cout << "accepting connections.\n";

    for(;;) {
        auto con = co_await server->accept();
        send_blocks(std::move(con), data, mmap, bufinv);
    }
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    doca::logger->set_level(spdlog::level::warn);

    auto engine = doca::progress_engine{};
    serve(&engine);
    engine.main_loop();
}