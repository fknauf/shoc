#include "env.hpp"

#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/comch/client.hpp>
#include <shoc/device.hpp>
#include <shoc/progress_engine.hpp>
#include <shoc/rdma.hpp>

#include <boost/asio.hpp>
#include <boost/cobalt.hpp>

#include <iostream>
#include <string_view>
#include <vector>

auto rdma_exchange_connection_details(
    std::span<std::byte const> local_conn_details,
    std::string const &remote_address
) -> boost::cobalt::promise<std::vector<std::byte>> {
    using boost::asio::ip::tcp;
    using boost::asio::detached;
    using tcp_resolver = boost::cobalt::use_op_t::as_default_on_t<tcp::resolver>;
    using tcp_socket = boost::cobalt::use_op_t::as_default_on_t<tcp::socket>;
    
    auto strand = co_await boost::cobalt::this_coro::executor;
    auto resolver = tcp_resolver { strand };
    auto endpoints = co_await resolver.async_resolve(remote_address, "12345");

    shoc::logger->debug("connecting for details exchange...");

    auto sock = tcp_socket { strand };
    co_await boost::asio::async_connect(sock, endpoints);

    shoc::logger->debug("connected. Sending local details...");

    co_await async_write(sock, boost::asio::buffer(local_conn_details));

    shoc::logger->debug("connected. Details sent. Receiving remote details...");

    std::byte received_buffer[4096];
    auto bytes_received = co_await sock.async_read_some(boost::asio::buffer(received_buffer));

    shoc::logger->debug("Received.");

    auto bytes = std::vector<std::byte>(received_buffer, received_buffer + bytes_received);

    co_return bytes;
}

auto rdma_send(
    shoc::progress_engine_lease engine,
    shoc::ibdev_name ibdev_name,
    std::string const &remote_address
) -> boost::cobalt::detached try {
    auto dev = shoc::device::find(ibdev_name, shoc::device_capability::rdma);

    auto rdma = co_await shoc::rdma_context::create(engine, dev);
    auto conn = rdma->export_connection();

    shoc::logger->debug("exchanging connection details...");

    auto remote_conn_details = co_await rdma_exchange_connection_details(conn.details(), remote_address);

    shoc::logger->debug("exchanged connection details, connecting...");

    conn.connect(remote_conn_details);

    shoc::logger->debug("connected.");

    auto data = std::string { "Hello, bRainDMAged." };
    auto mmap = shoc::memory_map { dev, data };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto send_buf = bufinv.buf_get_by_data(mmap, data);

    shoc::logger->debug("sending data...");

    auto err = co_await conn.send(send_buf, 42);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failed to send data: {}", doca_error_get_descr(err));
    } else {
        shoc::logger->debug("data sent.");
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
    if(argc < 2) {
        std::cerr << "Usage: " << argv[0] << " REMOTE_ADDRESS" << std::endl;
    }

    shoc::set_sdk_log_level(DOCA_LOG_LEVEL_DEBUG);
    shoc::logger->set_level(spdlog::level::debug);

    auto env = bluefield_env{};
    auto engine = shoc::progress_engine{};

    rdma_send(&engine, env.ibdev_name, argv[1]);

    co_await engine.run();
}
