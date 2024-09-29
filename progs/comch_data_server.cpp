#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/producer.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/logger.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <iostream>
#include <string_view>
#include <vector>

auto dance(
    doca::comch::scoped_server_connection con,
    doca::comch::comch_device &dev
) -> doca::coro::fiber {
    auto msg = co_await con->msg_recv();

    if(
        msg == "give x" &&
        co_await con->send("ok") == DOCA_SUCCESS
    ) {
        auto consumer_id = co_await con->accept_consumer();

        auto prod = co_await con->create_producer(16);

        auto memory = std::vector<char>(1024, 'x');
        auto mmap = doca::memory_map { dev, memory, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
        auto bufinv = doca::buffer_inventory { 1 };
        auto buffer = bufinv.buf_get_by_data(mmap, memory);

        auto status = co_await prod->send(buffer, {}, consumer_id);

        if(status != DOCA_SUCCESS) {
            doca::logger->warn("producer failed to send buffer: {}", doca_error_get_descr(status));
        }
    }

    co_await con->disconnect();
}

auto serve(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::comch::comch_device { "03:00.0" };
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("vss-data-test", dev, rep);

    for(;;) {
        auto con = co_await server->accept();
        dance(std::move(con), dev);
    }
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    doca::logger->set_level(spdlog::level::debug);

    auto engine = doca::progress_engine{};

    serve(&engine);

    engine.main_loop();
}