#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_aes_gcm.h>

#include <cstdint>
#include <span>

namespace doca {
    class aes_gcm_context;

    class aes_gcm_key {
    public:
        aes_gcm_key(
            aes_gcm_context const &parent,
            std::span<std::byte const> key_data,
            doca_aes_gcm_key_type key_type
        );

        [[nodiscard]] auto handle() const noexcept {
            return handle_.handle();
        }

    private:
        unique_handle<doca_aes_gcm_key, doca_aes_gcm_key_destroy> handle_;
    };

    class aes_gcm_context:
        public context
    {
    public:
        aes_gcm_context(
            context_parent *parent,
            device const &dev,
            std::uint32_t num_tasks
        );

        [[nodiscard]] auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_aes_gcm_as_ctx(handle_.handle());
        }

        [[nodiscard]] auto handle() const noexcept {
            return handle_.handle();
        }

        [[nodiscard]] auto encrypt(
            buffer plaintext,
            buffer dest,
            aes_gcm_key const &key,
            std::span<std::byte const> iv,
            std::uint32_t tag_size,
            std::uint32_t aad_size = 0
        ) -> coro::status_awaitable<>;

        [[nodiscard]] auto decrypt(
            buffer encrypted,
            buffer dest,
            aes_gcm_key const &key,
            std::span<std::byte const> iv,
            std::uint32_t tag_size,
            std::uint32_t aad_size = 0
        ) -> coro::status_awaitable<>;

    private:
        unique_handle<doca_aes_gcm, doca_aes_gcm_destroy> handle_;
    };
}
