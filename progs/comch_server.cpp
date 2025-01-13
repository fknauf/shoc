
#include "shoc/comch/server.hpp"
#include "shoc/coro/fiber.hpp"
#include "shoc/coro/task.hpp"
#include "shoc/progress_engine.hpp"

#include <iostream>
#include <string_view>

auto ping_pong(shoc::comch::scoped_server_connection con) -> shoc::coro::fiber {
    auto msg = co_await con->msg_recv();
    std::cout << msg << std::endl;
    auto status = co_await con->send("pong");

    if(status != DOCA_SUCCESS) {
        shoc::logger->error("failed to send response:", doca_error_get_descr(status));
    }
}

auto serve_ping_pong(shoc::progress_engine *engine) -> shoc::coro::fiber {
    auto dev = shoc::device::find_by_pci_addr("03:00.0", shoc::device_capability::comch_server);
    auto rep = shoc::device_representor::find_by_pci_addr ( dev, "81:00.0", DOCA_DEVINFO_REP_FILTER_NET );

    auto server = co_await engine->create_context<shoc::comch::server>("shoc-test", dev, rep);

    for(;;) {
        // wait for and accept client connection
        auto con = co_await server->accept();
        // spawn new coroutine (fiber) to handle it. Again this will run up to the first co_await
        // and suspend, returning control here. It will be resumed when events concerning that
        // connection are processed by the engine.
        ping_pong(std::move(con));
    }
}

int main() {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    shoc::logger->set_level(spdlog::level::info);

    auto engine = shoc::progress_engine{};
    serve_ping_pong(&engine);

    engine.main_loop();
}
