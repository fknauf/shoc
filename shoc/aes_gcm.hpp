#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_aes_gcm.h>

#include <cstdint>
#include <span>

/**
 * Cryptography functionality with AES-GCM, see https://docs.nvidia.com/doca/sdk/doca+aes-gcm/index.html
 */
namespace shoc {
    class aes_gcm_context;

    /**
     * Handle to a loaded key. Must be created through a aes_gcm_context because we need to clean
     * up the keys before we stop the context, and so the context has to know its keys.
     */
    class aes_gcm_key {
    public:
        aes_gcm_key() noexcept = default;
        aes_gcm_key(aes_gcm_key &&other) noexcept;
        aes_gcm_key &operator=(aes_gcm_key &&other) noexcept;
        ~aes_gcm_key();

        /**
         * @return plain-DOCA handle to the loaded key. For internal use. In particular,
         * don't modify the key through this handle, or we may end up in unexpected states.
         */
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
     * Context for AES-GCM operations on crypto-enabled Bluefields.
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

        /**
         * Load a crypto hey from raw bytes
         *
         * @param key_data key bytes
         * @param key_type type of the key (128 bit or 256 bit)
         */
        [[nodiscard]] auto load_key(
            std::span<std::byte const> key_data,
            doca_aes_gcm_key_type key_type
        ) -> aes_gcm_key;

        /**
         * Offload a task to encrypt a data buffer with AES-GCM.
         *
         * @param plaintext input buffer with plaintext. First aad_size bytes are considered AAD and only authenticated, not encrypted.
         * @param dest output buffer for cryptotext, needs to be at least as big as plaintext plus space for tag
         * @param key key to use for encryption
         * @param iv initialisation vector for the GCM cipher mode
         * @param tag_size size of the authentication tag, either 12 or 16 bytes
         * @param aad_size size of the additional authenticated data at the beginning of plaintext in bytes.
         */
        [[nodiscard]] auto encrypt(
            buffer plaintext,
            buffer dest,
            aes_gcm_key const &key,
            std::span<std::byte const> iv,
            std::uint32_t tag_size,
            std::uint32_t aad_size = 0
        ) -> coro::status_awaitable<>;

        /**
         * Offload a task to decrypt a data buffer with AES-GCM
         *
         * @param encrypted input buffer with ciphertext (except for the first aad_size bytes, which are considered AAD)
         * @param dest output buffer for plaintext, needs to be as big as encrypted minus tag_size
         * @param key key to use for decryption
         * @param iv initialisation vector for the GCM cipher mode
         * @param tag_size size of the authentication tag (12 or 16 bytes)
         * @param aad_size size of the additional authenticated data at the beginning of encrypted a in bytes
         */
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
