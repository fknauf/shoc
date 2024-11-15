#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/client.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/device.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>
#include <doca/sync_event.hpp>

auto sync_event_remote(
    doca::progress_engine *engine
) -> doca::coro::fiber {
    auto err = doca_error_t { DOCA_SUCCESS };
    auto msg = std::string{};

#ifdef DOCA_ARCH_DPU
    auto dev = doca::device::find_by_pci_addr(
        "03:00.0",
        { 
            doca::device_capability::sync_event_pci,
            doca::device_capability::comch_server
        }
    );
    auto rep = doca::device_representor::find_by_pci_addr(dev, "81:00.0");

    auto server = co_await engine->create_context<doca::comch::server>("vss-sync-event-test", dev, rep);
    auto conn = co_await server->accept_connection();
    msg = co_await conn->msg_recv();
#else
    auto dev = doca::device::find_by_pci_addr(
        "81:00.0",
        {
            doca::device_capability::sync_event_pci,
            doca::device_capability::comch_client
        }
    );

    auto client = co_await engine->create_context<doca::comch::client>("vss-sync-event-test", dev);
    msg = co_await client->msg_recv();
#endif

    auto event_descriptor = std::span {
        reinterpret_cast<std::byte const*>(msg.data()),
        msg.size()
    };

    auto sync = co_await engine->create_context<doca::sync_event>(dev, event_descriptor, 16);

    err = co_await sync->notify_set(23);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("failure notifying peer: {}", doca_error_get_descr(err));
        co_return;
    }

    err = co_await sync->wait_eq(42);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("failure waiting for peer: {}", doca_error_get_descr(err));
        co_return;
    }
}

auto main() -> int {
    auto engine = doca::progress_engine {};
    sync_event_remote(&engine);
    engine.main_loop();
}
