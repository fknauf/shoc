#pragma once

#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "progress_engine.hpp"
#include "unique_handle.hpp"

#include <doca_sync_event.h>

#include <cstdint>
#include <ranges>
#include <span>
#include <variant>

namespace shoc {
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
        public context<
            doca_sync_event,
            doca_sync_event_destroy,
            doca_sync_event_as_ctx
        >
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

        [[nodiscard]]
        static auto create(
            progress_engine_lease &engine,
            sync_event_publisher_location publisher,
            sync_event_subscriber_location subscriber,
            std::uint32_t max_tasks
        ) {
            return engine.create_context<sync_event>(std::move(publisher), std::move(subscriber), max_tasks);
        }

        sync_event(
            context_parent *parent,
            std::ranges::range auto &&publishers,
            std::ranges::range auto &&subscribers,
            std::uint32_t max_tasks
        ):
            context { parent, context::create_doca_handle<doca_sync_event_create>() }
        {
            init_callbacks(max_tasks);

            for(auto &pub : publishers) {
                init_add_publisher(pub);
            }

            for(auto &sub : subscribers) {
                init_add_subscriber(sub);
            }
        }

        template<
            std::ranges::range PubRange,
            std::ranges::range SubRange
        >
        [[nodiscard]]
        static auto create(
            progress_engine_lease &engine,
            PubRange &&publishers,
            SubRange &&subscribers,
            std::uint32_t max_tasks
        ) {
            return engine.create_context<sync_event>(
                std::forward<PubRange>(publishers),
                std::forward<SubRange>(subscribers),
                max_tasks
            );
        }

        sync_event(
            context_parent *parent,
            device const &dev,
            std::span<std::byte const> export_data,
            std::uint32_t max_tasks
        );

        [[nodiscard]]
        static auto create(
            progress_engine_lease &engine,
            device const &dev,
            std::span<std::byte const> export_data,
            std::uint32_t max_tasks
        ) {
            return engine.create_context<sync_event>(dev, export_data, max_tasks);
        }

        [[nodiscard]]
        auto export_to_remote_pci(device const &dev) const -> std::span<std::byte const>;
        [[nodiscard]]
        auto export_to_remote_net() const -> std::span<std::byte const>;

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

        // to extend lifetime of the referenced DOCA devices
        std::vector<device> referenced_devices_;
    };

    class sync_event_remote_net {
    public:
        [[nodiscard]]
        static auto from_export(
            device const &dev,
            std::span<std::byte const> data_stream
        ) -> sync_event_remote_net;

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        sync_event_remote_net(doca_sync_event_remote_net *handle, device dev);

        device dev_;
        unique_handle<doca_sync_event_remote_net, doca_sync_event_remote_net_destroy> handle_;
    };
}
