#include <shoc/aes_gcm.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/device.hpp>
#include <shoc/logger.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <cxxopts.hpp>
#include <cppcodec/hex_lower.hpp>

#include <boost/cobalt.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

auto encrypt(
    shoc::progress_engine_lease engine,
    std::string input_filename,
    std::string output_filename,
    std::span<std::byte const> keybytes,
    std::span<std::byte const> iv
) -> boost::cobalt::detached {
    auto dev = shoc::device::find(shoc::device_capability::aes_gcm);
    auto ctx = co_await engine->create_context<shoc::aes_gcm_context>(dev, 16);

    auto blocksize = std::size_t { 1 << 20 };
    auto memory = std::vector<std::byte>(blocksize * 2, std::byte{});
    auto memmap = shoc::memory_map { dev, memory };
    auto bufinv = shoc::buffer_inventory { 2 };

    auto inmem = std::span { memory }.subspan(0, blocksize);
    auto outmem = std::span { memory }.subspan(blocksize, blocksize);

    auto in = std::ifstream { input_filename, std::ios::binary };
    auto out = std::ofstream { output_filename, std::ios::binary };

    auto key = ctx->load_key(keybytes, keybytes.size() == 32 ? DOCA_AES_GCM_KEY_256 : DOCA_AES_GCM_KEY_128);

    while(in.read(reinterpret_cast<char*>(inmem.data()), inmem.size())) {
        auto n = in.gcount();

        auto inbuf = bufinv.buf_get_by_data(memmap, inmem.subspan(0, n));
        auto outbuf = bufinv.buf_get_by_addr(memmap, outmem);

        co_await ctx->encrypt(inbuf, outbuf, key, iv, 12);

        out.write(outbuf.data().data(), outbuf.data().size());
    }
}

auto co_main(
    int argc,
    char *argv[]
) -> boost::cobalt::main {
    auto options = cxxopts::Options("shoc-encrypt", "AES-GCM encryption sample program");

    options.add_options()
        ("k,key", "key string (hex)", cxxopts::value<std::string>())
        ("i,input", "input file", cxxopts::value<std::string>())
        ("o,output", "output file", cxxopts::value<std::string>())
        ("iv", "init vector (hex)", cxxopts::value<std::string>()->default_value(""))
        ;

    auto cmdline = options.parse(argc, argv);
    auto key = cppcodec::hex_lower::decode<std::vector<std::uint8_t>>(cmdline["key"].as<std::string>());
    auto iv = cppcodec::hex_lower::decode<std::vector<std::uint8_t>>(cmdline["iv"].as<std::string>());

    if(key.size() != 16 && key.size() != 32) {
        shoc::logger->error("key must be either 128 or 256 bits (16 or 32 bytes) long");
        co_return -1;
    }

    if(iv.size() == 0) {
        iv.resize(key.size(), std::uint8_t{0});
    } else if(iv.size() != key.size()) {
        shoc::logger->error("IV must have same length as key");
        co_return -1;
    }

    auto engine = shoc::progress_engine {};

    encrypt(
        &engine,
        cmdline["input"].as<std::string>(),
        cmdline["output"].as<std::string>(),
        std::as_bytes(std::span{key}),
        std::as_bytes(std::span{iv})
    );

    co_await engine.run();    
}
