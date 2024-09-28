#include "doca/comch/client.hpp"
#include "doca/coro/fiber.hpp"
#include "doca/logger.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>

auto ping_pong(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::comch::comch_device { "81:00.0" };

    auto client = co_await engine->create_context<doca::comch::client>("vss-test", dev);

    doca::logger->debug("sending ping...");

    auto status = co_await client->send("ping");

    if(status != DOCA_SUCCESS) {
        doca::logger->error("could not send ping: = {}", doca_error_get_descr(status));
        co_return;
    }

    auto msg = co_await client->msg_recv();

    if(msg) {
        std::cout << *msg << std::endl;
        co_await client->stop();
    } else {
        doca::logger->warn("no message received before disconnection");
    }
}

int main() {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::trace);

    auto engine = doca::progress_engine {};

    ping_pong(&engine);

    engine.main_loop();
}
