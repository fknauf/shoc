#include "doca/comch/client.hpp"
#include "doca/coro/task.hpp"
#include "doca/logger.hpp"
#include "doca/progress_engine.hpp"

#include <latch>
#include <iostream>

auto ping_pong(doca::progress_engine *engine) -> doca::coro::eager_task<void> {
    auto dev = doca::comch::comch_device { "81:00.0" };

    auto client = co_await engine->create_context<doca::comch::client("vss-test", dev);

    client->send_message("ping");

    auto [ msg, con ] = co_await client->recv_msg();

    std::cout << msg << std::endl;

    co_await client->stop();
}

int main() {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::debug);    
    
    auto engine = doca::progress_engine {};

    auto client_task = ping_pong(&engine);

    engine.main_loop();
}
