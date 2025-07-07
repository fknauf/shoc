#include "aes_gcm.hpp"

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
        aes_gcm_context *parent,
        std::span<std::byte const> key_data,
        doca_aes_gcm_key_type key_type
    ):
        parent_ { parent }
    {
        doca_aes_gcm_key *key;

        enforce(expected_key_size(key_type) == key_data.size(), DOCA_ERROR_INVALID_VALUE);

        enforce_success(doca_aes_gcm_key_create(
            parent->handle(),
            key_data.data(),
            key_type,
            &key
        ));
        handle_.reset(key);
    }

    aes_gcm_key::aes_gcm_key(aes_gcm_key &&other) noexcept {
        *this = std::move(other);
    }

    aes_gcm_key &aes_gcm_key::operator=(aes_gcm_key &&other) noexcept {
        handle_ = std::move(other.handle_);
        parent_ = std::exchange(other.parent_, nullptr);
        return *this;
    }

    aes_gcm_key::~aes_gcm_key() {
        clear();
    }

    auto aes_gcm_key::clear() -> void {
        if(handle_.get() != nullptr) {
            handle_.reset(nullptr);

            assert(parent_ != nullptr);
            parent_->signal_key_destroyed();
            parent_ = nullptr;
        }
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

    auto aes_gcm_context::load_key(
        std::span<std::byte const> key_bytes,
        doca_aes_gcm_key_type key_type
    ) -> aes_gcm_key {
        auto key = aes_gcm_key { this, key_bytes, key_type };
        ++loaded_keys_;
        return key;
    }

    auto aes_gcm_context::stop() -> context_state_awaitable {
        stop_requested_ = true;
        do_stop_if_able();
        return context_state_awaitable { shared_from_this(), DOCA_CTX_STATE_IDLE };
    }

    auto aes_gcm_context::signal_key_destroyed() -> void {
        assert(loaded_keys_ > 0);
        --loaded_keys_;
        if(stop_requested_) {
            do_stop_if_able();
        }
    }

    auto aes_gcm_context::do_stop_if_able() -> void {
        if(loaded_keys_ > 0) {
            return;
        }

        if(handle_.get() == nullptr) {
            logger->warn("tried to double-stop aes-gcm context");
            return;
        }

        auto err = doca_ctx_stop(as_ctx());

        if(err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS) {
            logger->error("unable to stop aes-gcm context even though all keys are destroyed");
        }
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
