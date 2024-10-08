#include <doca/buffer.hpp>
#include <doca/buffer_inventory.hpp>
#include <doca/comch/server.hpp>
#include <doca/coro/fiber.hpp>
#include <doca/dma.hpp>
#include <doca/memory_map.hpp>
#include <doca/progress_engine.hpp>

#include <span>
#include <vector>

struct remote_buffer_descriptor {
    std::span<char> src_buffer_range;
    doca::memory_map::export_descriptor export_desc;
};

auto parse_remote_buffer_descr(std::string const &msg) {
    auto src_buffer_addr = std::uint64_t{};
    auto src_buffer_len = std::uint64_t{};

    std::copy(msg.data()    , msg.data() +  8, &src_buffer_addr);
    std::copy(msg.data() + 8, msg.data() + 16, &src_buffer_len );

    auto export_desc = doca::memory_map::export_descriptor {
        .base_ptr = msg.data() + 16,
        .length = msg.size() - 16
    };

    return remote_buffer_descriptor {
        .src_buffer_range = {
            reinterpret_cast<char*>(src_buffer_addr),
            src_buffer_len
        },
        .export_desc = export_desc
    };
}

auto dma_serve(doca::progress_engine *engine) {
    auto dev = doca::device::find_by_pci_addr("03:00.0", { doca::device_capability::dma, doca::device_capability::comch_server });
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto server = co_await engine->create_context<doca::comch::server>("dma-test", dev, rep);
    auto dma = co_await engine->create_context<doca::dma_context>(dev, 16);

    auto conn = co_await server->accept();
    auto msg = co_await conn->msg_recv();
    auto remote_buffer_desc = parse_remote_buffer_descr(msg);

    auto remote_mmap = doca::memory_map { dev, remote_buffer_desc.export_desc };
    auto inv = doca::buffer_inventory { 1 };
    auto buf = inv.buf_get_by_addr(remote_mmap, remote_buffer_desc.src_buffer_range);

    auto local_mmap = doca::memory_map { }

    auto ok_status = co_await conn->send("ok");
}

auto main() -> int {
    auto engine = doca::progress_engine{};

    dma_serve(&engine);

    engine.main_loop();
}