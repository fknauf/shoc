#pragma once

#include "value_awaitable.hpp"

#include <shoc/common/overload.hpp>
#include <shoc/error.hpp>

#include <asio/awaitable.hpp>

#include <concepts>
#include <coroutine>
#include <memory>
#include <variant>

/**
 * This module defines an awaitable whose result is a status code (doca_error_t).
 *
 * Calls that use these will return an error code when something in the DOCA API returns
 * an error and only throw exceptions for errors that don't originate from DOCA. When an
 * operation generates a status code in addition to optional additional data, a buffer that
 * will accept these data can be supplied, as in the compress subsystem, where after
 *
 * auto status = co_await ctx->compress(src, dest, &checksums);
 *
 * status will be the error code (e.g. DOCA_SUCCESS), dest will contain the compressed
 * data, and checksums will contain the CRC and Adler checksums calculated during compression.
 */
namespace shoc::coro {
    /**
     * Reference to additional data, if any. This is a class of its own largely to get
     * around the compiler's complaints about void being an incomplete type in the naive
     * implementation.
     */
    template<typename AdditionalData>
    class additional_data_reference {
    public:
        additional_data_reference(AdditionalData *buf = nullptr):
            buf_ { buf }
        {}

        auto overwrite(AdditionalData &&data) {
            if(buf_ != nullptr) {
                *buf_ = std::move(data);
            }
        }

        explicit operator bool() const noexcept {
            return buf_ != nullptr;
        }

    private:
        AdditionalData *buf_;
    };

    /**
     * Special case: no additional data is expected.
     */
    template<>
    struct additional_data_reference<void> {
        additional_data_reference([[maybe_unused]] void *ptr = nullptr) {
        }
    };

    /**
     * Receptable for the status code. This is the meeting point between waiter and callback,
     * such that the waiter on suspend will register itself in this receptable upon suspend, and
     * the callback will set the value, overwrite the additional data buffer (if any), and then
     * resume the waiting coroutine.
     *
     * Only a single waiting coroutine is supported.
     *
     * The receptable will be owned by the awaitable, so the awaitable must be kept alive long
     * enough for the callback to write data into it.
     *
     * Not part of the API.
     */
    template<typename AdditionalData>
    class status_receptable:
        public value_receptable<doca_error_t>
    {
    public:
        status_receptable() = default;

        status_receptable(AdditionalData *additional_data_dest):
            additional_data_ { additional_data_dest }
        {}

        status_receptable(doca_error_t status):
            value_receptable(std::move(status))
        {}

        status_receptable(std::exception_ptr ex):
            value_receptable(ex)
        {}

        auto set_error(doca_error_t status) -> void override {
            emplace_value(status);
        }

        [[nodiscard]]
        auto additional_data() const {
            return additional_data_;
        }

    private:
        additional_data_reference<AdditionalData> additional_data_;
    };

    /**
     * Awaitable class for a status code with support for an additional data buffer.
     *
     * Must be co_awaited by exactly one coroutine during its lifetime. More technically: must
     * be kept alive long enough for a callback to fill the receptable, and only one waiting
     * coroutine is supported.
     *
     * Most functionality here is internal to the library; client code should only use co_await
     * on these objects and possibly move them around the place a bit before co_awaiting.
     *
     * TODO: Make the abstraction less leaky, so that client code can't use what it isn't supposed
     * to use.
     */
    template<typename AdditionalData = void>
    class [[nodiscard]] status_awaitable:
        public asio::awaitable<doca_error_t> {
    public:
        using payload_type = status_receptable<AdditionalData>;

        status_awaitable() = default;

        [[nodiscard]]
        static auto create_space(AdditionalData *additional_data_buffer = nullptr) {
            return status_awaitable { std::make_unique<payload_type>(additional_data_buffer) };
        }

        [[nodiscard]]
        static auto from_value(doca_error_t status) {
            return status_awaitable { std::make_unique<payload_type>(status) };
        }

        [[nodiscard]]
        static auto from_exception(std::exception_ptr ex) {
            return status_awaitable { std::make_unique<payload_type>(ex) };
        }

        [[nodiscard]]
        auto receptable_ptr() {
            return dest_.get();
        }

        [[nodiscard]]
        auto await_ready() const noexcept -> bool {
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);
            return dest_->has_value();
        }

        auto await_suspend(std::coroutine_handle<> handle) const -> void {
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);
            dest_->set_waiter(handle);
        }

        [[nodiscard]]
        auto await_resume() const {
            enforce(dest_ != nullptr, DOCA_ERROR_EMPTY);
            return dest_->value();
        }

    private:
        status_awaitable(std::unique_ptr<payload_type> &&dest):
            dest_ { std::move(dest) }
        {}

        std::unique_ptr<payload_type> dest_;
    };
}