#pragma once

#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_sync_event.h>

#include <cstdint>
#include <ranges>
#include <span>
#include <variant>

namespace doca {
    struct sync_event_location_pci {};
    struct sync_event_location_remote_net {};

    using sync_event_publisher_location = std::variant<
        sync_event_location_pci,
        sync_event_location_remote_net,
        std::reference_wrapper<device const>
    >;

    using sync_event_subscriber_location = std::variant<
        sync_event_location_pci,
        std::reference_wrapper<device const>
    >;

    class sync_event:
        public context
    {
    public:
        using location_pci = sync_event_location_pci;
        using location_remote_net = sync_event_location_remote_net;

        using publisher_location = sync_event_publisher_location;
        using subscriber_location = sync_event_subscriber_location;

        sync_event(
            context_parent *parent,
            sync_event_publisher_location publisher,
            sync_event_subscriber_location subscriber,
            std::uint32_t max_tasks
        );

        template<
            std::ranges::range Publishers,
            std::ranges::range Subscribers
        >
        sync_event(
            context_parent *parent,
            Publishers &&publishers,
            Subscribers &&subscribers,
            std::uint32_t max_tasks
        ):
            context { parent }
        {
            doca_sync_event *event;
            enforce_success(doca_sync_event_create(&event));
            handle_.reset(event);

            init_callbacks(max_tasks);

            for(auto &pub : publishers) {
                init_add_publisher(pub);
            }

            for(auto &sub : subscribers) {
                init_add_subscriber(sub);
            }
        }

        [[nodiscard]]
        auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_sync_event_as_ctx(handle_.handle());
        }

        [[nodiscard]]
        auto handle() const noexcept { return handle_.handle(); }

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
            std::uint64_t mask = std::uint64_t(-1)
        ) const -> coro::status_awaitable<>;

        [[nodiscard]]
        auto wait_neq(
            std::uint64_t wait_val,
            std::uint64_t mask = std::uint64_t(-1)
        ) const -> coro::status_awaitable<>;

    private:
        auto init_callbacks(std::uint32_t max_tasks) -> void;
        auto init_add_publisher(publisher_location const &publisher) -> void;
        auto init_add_subscriber(subscriber_location const &subscriber) -> void;

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
