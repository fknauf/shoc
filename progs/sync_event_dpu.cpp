#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/device.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>
#include <doca/sync_event.hpp>

auto sync_event_dpu(doca::progress_engine *engine) -> doca::coro::fiber {
    auto dev = doca::device::find_by_pci_addr(
        "03:00.0",
        { 
            doca::device_capability::sync_event_pci,
            doca::device_capability::comch_server
        }
    );
    auto rep = doca::device_representor::find_by_pci_addr(dev, "81:00.0");

    auto sync = co_await engine->create_context<doca::sync_event>(dev, doca::sync_event::location_pci{}, 16);
    auto server = co_await engine->create_context<doca::comch::server>("vss-sync-event-test", dev, rep);

    [[maybe_unused]] auto event_descriptor = sync->export_to_remote_pci(dev);

    auto err = doca_error_t { DOCA_SUCCESS };

    err = co_await sync->wait_eq(23);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("failure waiting for peer: {}", doca_error_get_descr(err));
        co_return;
    }

    err = co_await sync->notify_set(42);

    if(err != DOCA_SUCCESS) {
        doca::logger->error("failure notifying peer: {}", doca_error_get_descr(err));
        co_return;
    }
}

auto main() -> int {
    auto engine = doca::progress_engine {};
    sync_event_dpu(&engine);
    engine.main_loop();
}
