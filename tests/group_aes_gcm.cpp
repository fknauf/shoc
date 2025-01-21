#include <shoc/aes_gcm.hpp>
#include <shoc/buffer.hpp>
#include <shoc/buffer_inventory.hpp>
#include <shoc/coro/fiber.hpp>
#include <shoc/device.hpp>
#include <shoc/memory_map.hpp>
#include <shoc/progress_engine.hpp>

#include <gtest/gtest.h>

#include <ranges>
#include <string>
#include <vector>

#define CO_FAIL(message) do { *report = (message); co_return; } while(false)
#define CO_ASSERT(condition, message) do { if(!(condition)) { CO_FAIL(message); } } while(false)

#define CO_ASSERT_EQ(val1, val2, message) CO_ASSERT((val1) == (val2), message)
#define CO_ASSERT_NE(val1, val2, message) CO_ASSERT((val1) != (val2), message)
#define CO_ASSERT_LT(val1, val2, message) CO_ASSERT((val1) <  (val2), message)
#define CO_ASSERT_LE(val1, val2, message) CO_ASSERT((val1) <= (val2), message)
#define CO_ASSERT_GT(val1, val2, message) CO_ASSERT((val1) >  (val2), message)
#define CO_ASSERT_GE(val1, val2, message) CO_ASSERT((val1) >= (val2), message)

TEST(docapp_aes_gcm, single_shot) {
    auto engine = shoc::progress_engine {};
    auto report = std::string { "fiber not started" };

    shoc::logger->set_level(spdlog::level::info);

    [](
        shoc::progress_engine *engine,
        std::string *report
    ) -> shoc::coro::fiber {
        try {
            *report = "";

            shoc::logger->info("starting fiber");

            auto dev = shoc::device::find_by_capabilities(shoc::device_capability::aes_gcm);

            shoc::logger->info("device found");

            auto buf_inv = shoc::buffer_inventory { 3 };

            auto iv = std::vector<std::byte>(12, std::byte{0});
            auto tag_size = std::size_t{12}; // 96 bit
            auto aad_size = std::size_t{0};

            auto src_data = std::string { "Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua." };
            auto src_mmap = shoc::memory_map { dev, src_data };
            auto src_buf = buf_inv.buf_get_by_data(src_mmap, src_data);
        
            auto dst_data = std::vector<char>(4096);
            auto dst_mmap = shoc::memory_map { dev, dst_data };
            auto dst_mid = dst_data.begin() + 2048;

            auto encrypted_buf = buf_inv.buf_get_by_addr(dst_mmap, std::span { dst_data.begin(), dst_mid });
            auto decrypted_buf = buf_inv.buf_get_by_addr(dst_mmap, std::span { dst_mid, dst_data.end() });

            auto ctx = co_await engine->create_context<shoc::aes_gcm_context>(dev, 1);

            shoc::logger->info("context created");

            auto raw_key = "abcdefghijklmnopqrstuvwxyz123456";
            auto key_bytes = std::span { reinterpret_cast<std::byte const *>(raw_key), 32 };
            auto key = ctx->load_key(key_bytes, DOCA_AES_GCM_KEY_256);
    
            auto err = co_await ctx->encrypt(src_buf, encrypted_buf, key, iv, tag_size, aad_size);

            shoc::logger->info("encryption finished");

            CO_ASSERT_EQ(DOCA_SUCCESS, err, std::string { "encryption failed: " } + doca_error_get_descr(err));

            auto expected_size = src_data.size() + tag_size;
            auto encrypted_size = encrypted_buf.data().size();

            CO_ASSERT_EQ(encrypted_size, expected_size, fmt::format("encrypted data size unexpected: {} != {}", encrypted_size, expected_size));

            err = co_await ctx->decrypt(encrypted_buf, decrypted_buf, key, iv, tag_size, aad_size);

            shoc::logger->info("decryption finished");

            CO_ASSERT_EQ(DOCA_SUCCESS, err, std::string { "decryption failed: " } + doca_error_get_descr(err));
            CO_ASSERT(std::ranges::equal(src_data, decrypted_buf.data()), "decrypted data are different from source data");
        } catch(std::exception &ex) {
            CO_FAIL(ex.what());
        } catch(...) {
            CO_FAIL("unknown error");
        }
    } (
        &engine,
        &report
    );

    engine.main_loop();

    ASSERT_EQ("", report);
}
