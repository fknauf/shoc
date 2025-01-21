#include "aes_gcm.hpp"

#include "common/status.hpp"
#include "error.hpp"
#include "progress_engine.hpp"

namespace shoc {
    namespace {
        auto expected_key_size(doca_aes_gcm_key_type key_type) -> std::size_t {
            switch(key_type) {
            case DOCA_AES_GCM_KEY_128:
                return 128 / 8;
            case DOCA_AES_GCM_KEY_256:
                return 256 / 8;
            default:
                throw doca_exception(DOCA_ERROR_INVALID_VALUE);
            }
        }
    }

    aes_gcm_key::aes_gcm_key(
        aes_gcm_context const &parent,
        std::span<std::byte const> key_data,
        doca_aes_gcm_key_type key_type
    ) {
        doca_aes_gcm_key *key;

        enforce(expected_key_size(key_type) == key_data.size(), DOCA_ERROR_INVALID_VALUE);

        enforce_success(doca_aes_gcm_key_create(
            parent.handle(),
            key_data.data(),
            key_type,
            &key
        ));
        handle_.reset(key);
    }

    aes_gcm_context::aes_gcm_context(
        context_parent *parent,
        device dev,
        std::uint32_t num_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_aes_gcm_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce_success(doca_aes_gcm_task_encrypt_set_conf(
            handle(),
            plain_status_callback<doca_aes_gcm_task_encrypt_as_task>,
            plain_status_callback<doca_aes_gcm_task_encrypt_as_task>,
            num_tasks
        ));

        enforce_success(doca_aes_gcm_task_decrypt_set_conf(
            handle(),
            plain_status_callback<doca_aes_gcm_task_decrypt_as_task>,
            plain_status_callback<doca_aes_gcm_task_decrypt_as_task>,
            num_tasks
        ));
    }

    auto aes_gcm_context::encrypt(
        buffer plaintext,
        buffer dest,
        aes_gcm_key const &key,
        std::span<std::byte const> iv,
        std::uint32_t tag_size,
        std::uint32_t aad_size
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_aes_gcm_task_encrypt_alloc_init,
            doca_aes_gcm_task_encrypt_as_task
        >(
            engine(),
            handle(),
            plaintext.handle(),
            dest.handle(),
            key.handle(),
            reinterpret_cast<std::uint8_t const*>(iv.data()),
            iv.size(),
            tag_size,
            aad_size
        );
    }

    auto aes_gcm_context::decrypt(
        buffer encrypted,
        buffer dest,
        aes_gcm_key const &key,
        std::span<std::byte const> iv,
        std::uint32_t tag_size,
        std::uint32_t aad_size
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_aes_gcm_task_decrypt_alloc_init,
            doca_aes_gcm_task_decrypt_as_task
        >(
            engine(),
            handle(),
            encrypted.handle(),
            dest.handle(),
            key.handle(),
            reinterpret_cast<std::uint8_t const*>(iv.data()),
            iv.size(),
            tag_size,
            aad_size
        );
    }
}
