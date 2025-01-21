#include "sync_event.hpp"

#include "common/status.hpp"
#include "progress_engine.hpp"

namespace shoc {
    sync_event::sync_event(
        context_parent *parent,
        device const &dev,
        std::span<std::byte const> export_data,
        std::uint32_t max_tasks
    ):
        context { 
            parent,
            context::create_doca_handle<doca_sync_event_create_from_export>(
                dev.handle(),
                reinterpret_cast<std::uint8_t const*>(export_data.data()),
                export_data.size()
            )
        },
        referenced_devices_ { dev }
    {
        init_callbacks(max_tasks);
    }

    sync_event::sync_event(
        context_parent *parent,
        sync_event_publisher_location publisher,
        sync_event_subscriber_location subscriber,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_sync_event_create>()
        }
    {
        init_callbacks(max_tasks);
        init_add_publisher(publisher);
        init_add_subscriber(subscriber);
    }

    auto sync_event::init_add_publisher(sync_event::publisher_location const &pub) -> void {
        enforce_success(std::visit(
            overload {
                [this](sync_event_location_pci) -> doca_error_t {
                    return doca_sync_event_add_publisher_location_remote_pci(handle());
                },
                [this](sync_event_location_remote_net) -> doca_error_t {
                    return doca_sync_event_add_publisher_location_remote_net(handle());
                },
                [this](device const &dev) -> doca_error_t {
                    referenced_devices_.push_back(dev);
                    return doca_sync_event_add_publisher_location_cpu(handle(), dev.handle());
                }
            },
            pub
        ));
    }

    auto sync_event::init_add_subscriber(sync_event::subscriber_location const &sub) -> void {
        enforce_success(std::visit(
            overload {
                [this](sync_event_location_pci) -> doca_error_t {
                    return doca_sync_event_add_subscriber_location_remote_pci(handle());
                },
                [this](device const &dev) -> doca_error_t {
                    referenced_devices_.push_back(dev);                    
                    return doca_sync_event_add_subscriber_location_cpu(handle(), dev.handle());
                }
            },
            sub
        ));
    }

    auto sync_event::init_callbacks(std::uint32_t max_tasks) -> void {
        enforce_success(doca_sync_event_task_get_set_conf(
            handle(),
            &plain_status_callback<doca_sync_event_task_get_as_doca_task>,
            &plain_status_callback<doca_sync_event_task_get_as_doca_task>,
            max_tasks
        ));

        enforce_success(doca_sync_event_task_notify_add_set_conf(
            handle(),
            &plain_status_callback<doca_sync_event_task_notify_add_as_doca_task>,
            &plain_status_callback<doca_sync_event_task_notify_add_as_doca_task>,
            max_tasks
        ));

        enforce_success(doca_sync_event_task_notify_set_set_conf(
            handle(),
            &plain_status_callback<doca_sync_event_task_notify_set_as_doca_task>,
            &plain_status_callback<doca_sync_event_task_notify_set_as_doca_task>,
            max_tasks
        ));

        enforce_success(doca_sync_event_task_wait_eq_set_conf(
            handle(),
            &plain_status_callback<doca_sync_event_task_wait_eq_as_doca_task>,
            &plain_status_callback<doca_sync_event_task_wait_eq_as_doca_task>,
            max_tasks
        ));

        enforce_success(doca_sync_event_task_wait_neq_set_conf(
            handle(),
            &plain_status_callback<doca_sync_event_task_wait_neq_as_doca_task>,
            &plain_status_callback<doca_sync_event_task_wait_neq_as_doca_task>,
            max_tasks
        ));
    }

    auto sync_event::export_to_remote_pci(device const &dev) const -> std::span<std::byte const> {
        std::byte const *base = nullptr;
        std::size_t size = 0;

        enforce_success(doca_sync_event_export_to_remote_pci(
            handle(),
            dev.handle(),
            reinterpret_cast<std::uint8_t const**>(&base),
            &size));

        return std::span { base, size };
    }

    auto sync_event::export_to_remote_net() const -> std::span<std::byte const> {
        std::byte const *base = nullptr;
        std::size_t size = 0;

        enforce_success(doca_sync_event_export_to_remote_net(
            handle(),
            reinterpret_cast<std::uint8_t const**>(&base),
            &size
        ));

        return std::span { base, size };
    }

    auto sync_event::get(std::uint64_t *dest) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sync_event_task_get_alloc_init,
            doca_sync_event_task_get_as_doca_task
        >(
            engine(),
            handle(),
            dest
        );
    }

    auto sync_event::notify_add(
        std::uint64_t inc_val,
        std::uint64_t *fetched
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sync_event_task_notify_add_alloc_init,
            doca_sync_event_task_notify_add_as_doca_task
        >(
            engine(),
            handle(),
            inc_val,
            fetched
        );
    }

    auto sync_event::notify_set(
        std::uint64_t set_val
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sync_event_task_notify_set_alloc_init,
            doca_sync_event_task_notify_set_as_doca_task
        >(
            engine(),
            handle(),
            set_val
        );
    }

    auto sync_event::wait_eq(
        std::uint64_t wait_val,
        std::uint64_t mask
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sync_event_task_wait_eq_alloc_init,
            doca_sync_event_task_wait_eq_as_doca_task
        >(
            engine(),
            handle(),
            wait_val,
            mask
        );
    }

    auto sync_event::wait_neq(
        std::uint64_t wait_val,
        std::uint64_t mask
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sync_event_task_wait_neq_alloc_init,
            doca_sync_event_task_wait_neq_as_doca_task
        >(
            engine(),
            handle(),
            wait_val,
            mask
        );
    }

    auto sync_event_remote_net::from_export(
        device const &dev,
        std::span<std::byte const> data_stream
    ) -> sync_event_remote_net {
        doca_sync_event_remote_net *event = nullptr;

        enforce_success(doca_sync_event_remote_net_create_from_export(
            dev.handle(),
            reinterpret_cast<std::uint8_t const*>(data_stream.data()),
            data_stream.size(),
            &event
        ));

        return sync_event_remote_net { event, dev };
    }

    sync_event_remote_net::sync_event_remote_net(
        doca_sync_event_remote_net *handle,
        device dev
    ):
        dev_ { std::move(dev) },
        handle_ { handle }
    { }
}
