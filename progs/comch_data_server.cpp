#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/producer.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/logger.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

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
    shoc::comch::scoped_server_connection con,
    data_descriptor &data,
    shoc::memory_map &mmap,
    shoc::buffer_inventory &bufinv
) -> shoc::coro::fiber {
    auto prod = co_await con->create_producer(16);
    auto send_status = co_await con->send(fmt::format("{} {}", data.block_count, data.block_size));

    if(send_status != DOCA_SUCCESS) {
        shoc::logger->error("failed to send data gemoetry");
        co_return;
    }

    auto consumer_id = co_await con->accept_consumer();

    for(auto i : std::ranges::views::iota(std::uint32_t{}, data.block_count)) {
        auto buffer = bufinv.buf_get_by_data(mmap, data.block(i));
        auto status = co_await prod->send(buffer, {}, consumer_id);

        if(status != DOCA_SUCCESS) {
            shoc::logger->error("producer failed to send buffer: {}", doca_error_get_descr(status));
            co_return;
        }
    }
}

auto serve(
    shoc::progress_engine *engine,
    char const *dev_pci,
    char const *rep_pci
) -> shoc::coro::fiber {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::comch_server);
    auto rep = shoc::device_representor::find_by_pci_addr(dev, rep_pci, DOCA_DEVINFO_REP_FILTER_NET);
    auto data = data_descriptor { 256, 1 << 20 };
    auto mmap = shoc::memory_map { dev, data.bytes, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = shoc::buffer_inventory { 32 };

    auto server = co_await engine->create_context<shoc::comch::server>("shoc-data-test", dev, rep);

    std::cout << "accepting connections.\n";

    for(;;) {
        auto con = co_await server->accept();
        send_blocks(std::move(con), data, mmap, bufinv);
    }
}

int main() {
    //shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    //shoc::logger->set_level(spdlog::level::warn);

    auto env_dev = std::getenv("DOCA_DEV_PCI");
    auto env_rep = std::getenv("DOCA_DEV_REP_PCI");

    auto engine = shoc::progress_engine{};
    serve(
        &engine,
        env_dev ? env_dev : "03:00.0",
        env_rep ? env_rep : "81:00.0"
    );
    engine.main_loop();
}
