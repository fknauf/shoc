
#include "doca/comch/server.hpp"
#include "doca/coro/fiber.hpp"
#include "doca/coro/task.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>
#include <string_view>

auto serve_ping_pong(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr("03:00.0", doca::device_capability::comch_server);
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0", DOCA_DEVINFO_REP_FILTER_NET );

    auto server = co_await engine->create_context<doca::comch::server>("vss-test", dev, rep);
    auto con = co_await server->accept();
    auto msg = co_await con->msg_recv();

    std::cout << msg << std::endl;

    auto status = co_await con->send("pong");

    if(status != DOCA_SUCCESS) {
        doca::logger->error("failed to send response:", doca_error_get_descr(status));
    }
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    doca::logger->set_level(spdlog::level::info);

    auto engine = doca::progress_engine{};
    serve_ping_pong(&engine);

    engine.main_loop();
}
