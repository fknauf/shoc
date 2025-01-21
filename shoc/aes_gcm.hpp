#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_aes_gcm.h>

#include <cstdint>
#include <span>

namespace shoc {
    class aes_gcm_context;

    class aes_gcm_key {
    public:
        aes_gcm_key() noexcept = default;
        aes_gcm_key(aes_gcm_key &&other) noexcept;
        aes_gcm_key &operator=(aes_gcm_key &&other) noexcept;
        ~aes_gcm_key();

        [[nodiscard]] auto handle() const noexcept {
            return handle_.get();
        }

        auto clear() -> void;

    private:
        friend class aes_gcm_context;

        aes_gcm_key(
            aes_gcm_context *parent,
            std::span<std::byte const> key_data,
            doca_aes_gcm_key_type key_type
        );

        unique_handle<doca_aes_gcm_key, doca_aes_gcm_key_destroy> handle_;
        aes_gcm_context *parent_ = nullptr;
    };

    /**
     * Context for AES-GCM operations on crypto-enabled Bluefields. Untested because it turned out
     * that our Bluefields are not crypto-enabled, so this should be considered something of a sketch.
     */
    class aes_gcm_context:
        public context<
            doca_aes_gcm,
            doca_aes_gcm_destroy,
            doca_aes_gcm_as_ctx
        >
    {
    public:
        aes_gcm_context(
            context_parent *parent,
            device dev,
            std::uint32_t num_tasks
        );

        [[nodiscard]] auto stop() -> context_state_awaitable override;

        [[nodiscard]] auto load_key(
            std::span<std::byte const> key_data,
            doca_aes_gcm_key_type key_type
        ) -> aes_gcm_key;

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
        friend class aes_gcm_key;

        auto signal_key_destroyed() -> void;
        auto do_stop_if_able() -> void;

        device dev_;
        int loaded_keys_ = 0;
        bool stop_requested_ = false;
    };
}
