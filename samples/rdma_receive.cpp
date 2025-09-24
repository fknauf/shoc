#include "env.hpp"

#include <shoc/aligned_memory.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/server.hpp>
#include <shoc/device.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/rdma.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <iostream>
#include <string_view>
#include <vector>

#include <fmt/printf.h>

auto rdma_exchange_connection_details(
    std::span<std::byte const> local_conn_details
) -> boost::cobalt::promise<std::vector<std::byte>> {
    using boost::asio::ip::tcp;
    using boost::asio::detached;
    using tcp_acceptor = boost::cobalt::use_op_t::as_default_on_t<tcp::acceptor>;

    auto executor = co_await boost::cobalt::this_coro::executor;
    
    auto listener = tcp_acceptor{ executor, { tcp::v4(), 12345 }};
    auto sock = co_await listener.async_accept();

    std::byte received_buffer[4096];
    auto bytes_received = co_await sock.async_read_some(boost::asio::buffer(received_buffer));
    auto bytes = std::vector<std::byte>(received_buffer, received_buffer + bytes_received);

    co_await boost::asio::async_write(sock, boost::asio::buffer(local_conn_details));

    co_return bytes;
}

auto rdma_receive(
    shoc::progress_engine_lease engine,
    shoc::ibdev_name ibdev_name
) -> boost::cobalt::detached try {
    auto dev = shoc::device::find(ibdev_name, shoc::device_capability::rdma);
    auto rdma = co_await shoc::rdma_context::create(engine, dev);
    auto conn = rdma->export_connection();

    auto remote_conn_details = co_await rdma_exchange_connection_details(conn.details());

    shoc::logger->debug("exchanged connection details, connecting...");

    conn.connect(remote_conn_details);

    shoc::logger->debug("connected.");

    auto membuffer = shoc::aligned_memory { 1024 };
    auto space = membuffer.as_writable_bytes();
    auto mmap = shoc::memory_map { dev, space };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto recv_buf = bufinv.buf_get_by_addr(mmap, space);

    std::uint32_t immediate_data = 0;

    shoc::logger->debug("receiving data...");

    auto err = co_await conn.receive(recv_buf, &immediate_data);

    shoc::logger->debug("data received.");

    if(err == DOCA_SUCCESS) {
        std::cout << std::string_view{ recv_buf.data().begin(), recv_buf.data().end() } << std::endl;
        std::cout << "imm = " << immediate_data;
    } else {
        shoc::logger->error("failed to receive data: {}", doca_error_get_descr(err));
    }
} catch(shoc::doca_exception &e) {
    shoc::logger->error("SHOC error: {}", e.what());
} catch(boost::system::error_code &e) {
    shoc::logger->error("Boost Error: {}", e.what());
} catch(std::exception &e) {
    shoc::logger->error("Generic Error: {}", e.what());
}

auto co_main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> boost::cobalt::main {
    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env{};
    auto engine = shoc::progress_engine{};

    rdma_receive(&engine, env.ibdev_name);

    co_await engine.run();
}
