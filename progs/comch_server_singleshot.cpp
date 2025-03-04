#include "env.hpp"

#include "shoc/comch/server.hpp"
#include "shoc/progress_engine.hpp"

#include <boost/cobalt.hpp>

#include <iostream>
#include <string_view>

auto serve_ping_pong(
    shoc::progress_engine_lease engine,
    char const *dev_pci,
    char const *rep_pci
) -> boost::cobalt::detached {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::comch_server);
    auto rep = shoc::device_representor::find_by_pci_addr ( dev, rep_pci, DOCA_DEVINFO_REP_FILTER_NET );

    auto server = co_await engine->create_context<shoc::comch::server>("shoc-test", dev, rep);
    auto con = co_await server->accept();
    auto msg = co_await con->msg_recv();

    std::cout << msg << std::endl;

    auto status = co_await con->send("pong");

    if(status != DOCA_SUCCESS) {
        shoc::logger->error("failed to send response:", doca_error_get_descr(status));
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_WARNING);
    shoc::logger->set_level(spdlog::level::info);

    auto env = bluefield_env_dpu{};
    auto engine = shoc::progress_engine{};

    serve_ping_pong(&engine, env.dev_pci, env.rep_pci);

    co_await engine.run();
}
