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
) -> boost::cobalt::promise<std::string> {
    using boost::asio::ip::tcp;
    using boost::asio::detached;
    using tcp_resolver = boost::cobalt::use_op_t::as_default_on_t<tcp::resolver>;
    using tcp_socket = boost::cobalt::use_op_t::as_default_on_t<tcp::socket>;
    
    auto strand = co_await boost::cobalt::this_coro::executor;
    auto resolver = tcp_resolver { strand };
    auto endpoints = co_await resolver.async_resolve(remote_address, "12345");

    auto sock = tcp_socket { strand };
    co_await boost::asio::async_connect(sock, endpoints);

    co_await async_write(sock, boost::asio::buffer(local_conn_details));

    std::byte received_buffer[256];
    auto bytes_received = co_await sock.async_read_some(boost::asio::buffer(received_buffer));
    auto bytes = std::vector<std::byte>(received_buffer, received_buffer + bytes_received);
}

auto rdma_send(
    shoc::progress_engine_lease engine,
    char const *dev_pci,
    std::string const &remote_address
) -> boost::cobalt::detached {
    auto dev = shoc::device::find_by_pci_addr(dev_pci, shoc::device_capability::rdma);

    auto rdma = co_await engine->create_context<shoc::rdma_context>(dev);
    auto conn = rdma->export_connection();

    auto remote_conn_details = co_await rdma_exchange_connection_details(conn.details(), remote_address);
    conn.connect(remote_conn_details);

    auto data = std::string { "Hello, bRainDMAged." };
    auto mmap = shoc::memory_map { dev, data };
    auto bufinv = shoc::buffer_inventory { 1 };
    auto send_buf = bufinv.buf_get_by_data(mmap, data);

    auto err = co_await conn.send(send_buf, 42);

    if(err != DOCA_SUCCESS) {
        shoc::logger->error("failed to send data: {}", doca_error_get_descr(err));
    }
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

    auto env = bluefield_env_host{};
    auto engine = shoc::progress_engine{};

    rdma_send(&engine, env.dev_pci, argv[1]);

    co_await engine.run();
}
