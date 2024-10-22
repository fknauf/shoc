#pragma once

#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_sync_event.h>

#include <cstdint>
#include <span>

namespace doca {
    class sync_event:
        public context
    {
    public:
        sync_event(
            context_parent *parent,
            device &subscriber_dev,
            std::uint32_t max_tasks
        );

        [[nodiscard]]
        auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_sync_event_as_ctx(handle_.handle());
        }

        [[nodiscard]]
        auto export_to_remote_pci(device const &dev) const -> std::span<std::uint8_t const>;
        [[nodiscard]]
        auto export_to_remote_net() const -> std::span<std::uint8_t const>;

        [[nodiscard]]
        auto get(std::uint64_t *dest) const -> coro::status_awaitable<>;

        [[nodiscard]]
        auto notify_add(
            std::uint64_t inc_val,
            std::uint64_t *fetched = nullptr
        ) const -> coro::status_awaitable<>;

        [[nodiscard]]
        auto notify_set(
            std::uint64_t set_val
        ) const -> coro::status_awaitable<>;

        [[nodiscard]]
        auto wait_eq(
            std::uint64_t wait_val,
            std::uint64_t mask
        ) const -> coro::status_awaitable<>;

        [[nodiscard]]
        auto wait_neq(
            std::uint64_t wait_val,
            std::uint64_t mask
        ) const -> coro::status_awaitable<>;

    private:
        unique_handle<doca_sync_event, doca_sync_event_destroy> handle_;
    };

    class sync_event_remote_net {
    public:
        [[nodiscard]]
        static auto from_export(
            device const &dev,
            std::span<std::uint8_t const> data_stream
        ) -> sync_event_remote_net;

        [[nodiscard]]
        auto handle() const noexcept { return handle_.handle(); }

    private:
        sync_event_remote_net(doca_sync_event_remote_net *handle);

        unique_handle<doca_sync_event_remote_net, doca_sync_event_remote_net_destroy> handle_;
    };
}
