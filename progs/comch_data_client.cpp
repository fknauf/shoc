#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/comch/consumer.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/logger.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <iostream>
#include <string_view>

auto ask_for_x(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("81:00.0", doca::device_capability::comch_client);

    auto client = co_await engine->create_context<doca::comch::client>("vss-data-test", dev);

    if(
        DOCA_SUCCESS == co_await client->send("give x") &&
        "ok" == co_await(client->msg_recv())
    ) {
        auto memory = std::vector<char>(1024);
        auto mmap = doca::memory_map { dev, memory, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
        auto bufinv = doca::buffer_inventory { 1 };
        auto buffer = bufinv.buf_get_by_addr(mmap, memory);

        auto consumer = co_await client->create_consumer(mmap, 16);
        auto result = co_await consumer->post_recv(buffer);

        if(result.status == DOCA_SUCCESS) {
            auto data = result.buf.data();
            std::cout << std::string_view { begin(data), end(data) } << std::endl;
        } else {
            doca::logger->error("post_recv failed with error: {}", doca_error_get_descr(result.status));
        }
    }
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    ask_for_x(&engine);

    engine.main_loop();

    doca::logger->info("done");
}
