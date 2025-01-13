#include "shoc/comch/client.hpp"
#include "shoc/coro/fiber.hpp"
#include "shoc/logger.hpp"
#include "shoc/progress_engine.hpp"

#include <iostream>

auto ping_pong(shoc::progress_engine *engine) -> shoc::coro::fiber {
    // get device from PCIe address
    auto dev = shoc::device::find_by_pci_addr("81:00.0", shoc::device_capability::comch_client);

    // wait for connection to server, that is: create the context and ask the SDK to start
    // it, then suspend. The coroutine will be resumed by the state-changed handler when
    // the client context switches to DOCA_CTX_STATE_RUNNING.
    auto client = co_await engine->create_context<shoc::comch::client>("shoc-test", dev);
    // send ping, wait for status result
    auto status = co_await client->send("ping");

    if(status != DOCA_SUCCESS) {
        shoc::logger->error("could not send ping: = {}", doca_error_get_descr(status));
        co_return;
    }

    // wait for response
    auto msg = co_await client->msg_recv();
    std::cout << msg << std::endl;

    // client stops automatically through RAII. If code needs to happen after the client
    // is stopped, co_await client->stop() is possible.
}

int main() {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    shoc::logger->set_level(spdlog::level::info);

    auto engine = shoc::progress_engine {};

    // spawn coroutine. It will run up to the first co_await, then control returns to main.
    ping_pong(&engine);

    // start event processing loop. This will resume the suspended coroutine whenever an
    // event is processed that concerns it. By default the main loop runs until all
    // dependent contexts are stopped.
    engine.main_loop();
}
