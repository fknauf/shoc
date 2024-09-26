
#include "doca/comch/server.hpp"
#include "doca/coro/fiber.hpp"
#include "doca/coro/task.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>
#include <string_view>

auto ping_pong(doca::comch::server_connection con) -> doca::coro::fiber {
    doca::logger->debug("waiting for message");

    auto msg = co_await con.msg_recv();

    doca::logger->debug("message received");

    if(msg) {
        std::cout << *msg << std::endl;
        auto status = co_await con.send("pong");
        doca::logger->info("sent response, status = {}", status);
    } else {
        doca::logger->warn("null message received, i.e. connection broke off");
    }

    auto err = con.disconnect();

    if(err != DOCA_SUCCESS) {
        doca::logger->warn("disconnect failed, err = {}", err);
    }
}

auto serve_ping_pong(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::comch::comch_device { "03:00.0" };
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    doca::logger->info("server starting");

    auto server = co_await engine->create_context<doca::comch::server>("vss-test", dev, rep);

    doca::logger->info("server started");

    for(;;) {
        auto con = co_await server->accept();
        ping_pong(std::move(con));
    }
}

int main() {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::trace);

    auto engine = doca::progress_engine{};
    serve_ping_pong(&engine);

    engine.main_loop();
}
