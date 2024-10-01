#include "doca/comch/client.hpp"
#include "doca/coro/fiber.hpp"
#include "doca/logger.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>

auto ping_pong(doca::progress_engine *engine) -> doca::coro::fiber {
    // get device from PCIe address
    auto dev = doca::comch::comch_device { "81:00.0" };

    // wait for connection to server, that is: create the context and ask the SDK to start
    // it, then suspend. The coroutine will be resumed by the state-changed handler when
    // the client context switches to DOCA_CTX_STATE_RUNNING.
    auto client = co_await engine->create_context<doca::comch::client>("vss-test", dev);
    // send ping, wait for status result
    auto status = co_await client->send("ping");

    if(status != DOCA_SUCCESS) {
        doca::logger->error("could not send ping: = {}", doca_error_get_descr(status));
        co_return;
    }

    // wait for response
    auto msg = co_await client->msg_recv();
    std::cout << msg << std::endl;

    // client stops automatically through RAII. If code needs to happen after the client
    // is stopped, co_await client->stop() is possible.
}

int main() {
    doca::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    doca::logger->set_level(spdlog::level::info);

    auto engine = doca::progress_engine {};

    // spawn coroutine. It will run up to the first co_await, then control returns to main.
    ping_pong(&engine);

    // start event processing loop. This will resume the suspended coroutine whenever an
    // event is processed that concerns it. By default the main loop runs until all
    // dependent contexts are stopped.
    engine.main_loop();
}
