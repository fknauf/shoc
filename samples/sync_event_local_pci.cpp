#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/device.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/sync_event.hpp>

#include <boost/cobalt.hpp>

auto sync_event_local(
    shoc::progress_engine_lease engine,
    bluefield_env env
) -> boost::cobalt::detached {
    auto err = doca_error_t { DOCA_SUCCESS };

#ifdef DOCA_ARCH_DPU
    auto dev = shoc::device::find(
        env.dev_pci,
        shoc::device_capability::sync_event_pci,
        shoc::device_capability::comch_server
    );
    auto rep = shoc::device_representor::find_by_pci_addr(dev, env.rep_pci);

    auto sync = co_await shoc::sync_event::create(engine, dev, shoc::sync_event::location_pci{}, 16);
    [[maybe_unused]] auto event_descriptor = sync->export_to_remote_pci(dev);

    auto server = co_await shoc::comch::server::create(engine, "shoc-sync-event-test", dev, rep);
    auto conn = co_await server->accept();
    err = co_await conn->send(event_descriptor);
#else
    auto dev = shoc::device::find(
        env.dev_pci,
        shoc::device_capability::sync_event_pci,
        shoc::device_capability::comch_client
    );

    auto sync = co_await shoc::sync_event::create(engine, dev, shoc::sync_event::location_pci{}, 16);
    [[maybe_unused]] auto event_descriptor = sync->export_to_remote_pci(dev);

    auto client = co_await shoc::comch::client::create(engine, "shoc-sync-event-test", dev);
    err = co_await client->send(event_descriptor);
#endif

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failure during cc handshake: {}", doca_error_get_descr(err));
        co_return;
    }

    err = co_await sync->wait_eq(23);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failure waiting for peer: {}", doca_error_get_descr(err));
        co_return;
    }

    err = co_await sync->notify_set(42);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failure notifying peer: {}", doca_error_get_descr(err));
        co_return;
    }
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    auto env = bluefield_env{};
    auto engine = shoc::progress_engine{};
    
    sync_event_local(&engine, env);
    
    co_await engine.run();
}
