#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/device.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/sync_event.hpp>

#include <asio.hpp>

auto sync_event_remote(
    shoc::progress_engine *engine,
    bluefield_env env
) -> shoc::coro::fiber {
    auto err = doca_error_t { DOCA_SUCCESS };
    auto msg = std::string{};

#ifdef DOCA_ARCH_DPU
    auto dev = shoc::device::find_by_pci_addr(
        env.dev_pci,
        { 
            shoc::device_capability::sync_event_pci,
            shoc::device_capability::comch_server
        }
    );
    auto rep = shoc::device_representor::find_by_pci_addr(dev, env.rep_pci);

    auto server = co_await engine->create_context<shoc::comch::server>("shoc-sync-event-test", dev, rep);
    auto conn = co_await server->accept();
    msg = co_await conn->msg_recv();
#else
    auto dev = shoc::device::find_by_pci_addr(
        env.dev_pci,
        {
            shoc::device_capability::sync_event_pci,
            shoc::device_capability::comch_client
        }
    );

    auto client = co_await engine->create_context<shoc::comch::client>("shoc-sync-event-test", dev);
    msg = co_await client->msg_recv();
#endif

    auto event_descriptor = std::span {
        reinterpret_cast<std::byte const*>(msg.data()),
        msg.size()
    };

    auto sync = co_await engine->create_context<shoc::sync_event>(dev, event_descriptor, 16);

    err = co_await sync->notify_set(23);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failure notifying peer: {}", doca_error_get_descr(err));
        co_return;
    }

    err = co_await sync->wait_eq(42);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failure waiting for peer: {}", doca_error_get_descr(err));
        co_return;
    }
}

auto main() -> int {
    auto env = bluefield_env{};
    auto io = asio::io_context{};
    auto engine = shoc::progress_engine{ io };
    sync_event_remote(&engine, env);
    io.run();
}
