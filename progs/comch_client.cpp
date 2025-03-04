#include "env.hpp"

#include "shoc/comch/client.hpp"
#include "shoc/logger.hpp"
#include "shoc/progress_engine.hpp"

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <iostream>

#include <sys/stat.h>

struct stat shoc_stat_buffer;

auto ping_pong(
    shoc::progress_engine_lease engine,
    char const *dev_pci
) -> boost::cobalt::detached {
    auto executor = co_await boost::cobalt::this_coro::executor;

    // get device from PCIe address
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::comch_client);

    for([[maybe_unused]] auto i : std::ranges::views::iota(0, 4)) {
        // wait for connection to server, that is: create the context and ask the SDK to start
        // it, then suspend. The coroutine will be resumed by the state-changed handler when
        // the client context switches to DOCA_CTX_STATE_RUNNING.
        std::cout << "connecting... pe active = " << engine->active() << std::endl;
        auto client = co_await engine->create_context<shoc::comch::client>("shoc-test", dev);
        std::cout << "connected." << std::endl;

        // send ping, wait for status result
        auto status = co_await client->send("ping");

        if(status != DOCA_SUCCESS) {
            shoc::logger->error("could not send ping: = {}", doca_error_get_descr(status));
            co_return;
        }

        std::cout << "yielding..." << std::endl;
        co_await engine->yield();
        //co_await boost::asio::steady_timer(co_await boost::cobalt::this_coro::executor, std::chrono::seconds(1)).async_wait(boost::cobalt::use_op);
        std::cout << "resumed." << std::endl;

        // wait for response
        auto msg = co_await client->msg_recv();
        std::cout << msg << std::endl;

        co_await client->stop();
        std::cout << "stopped client" << std::endl;
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main
{
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env_host{};
    auto engine = shoc::progress_engine { };

    // spawn coroutine. It will run up to the first co_await, then control returns to main.
    ping_pong(&engine, env.dev_pci);
    
    co_await engine.run();
}
