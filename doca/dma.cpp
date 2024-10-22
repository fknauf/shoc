#include "dma.hpp"
#include "error.hpp"

#include <doca/common/status.hpp>
#include <doca/progress_engine.hpp>

#include <doca_pe.h>

#include <cassert>

namespace doca {
    dma_context::dma_context(
        context_parent *parent,
        device const &dev,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        enforce(dev.has_capability(device_capability::dma), DOCA_ERROR_NOT_SUPPORTED);

        doca_dma *raw_handle;
        enforce_success(doca_dma_create(dev.handle(), &raw_handle));
        handle_.reset(raw_handle);

        init_state_changed_callback();
        enforce_success(doca_dma_task_memcpy_set_conf(
            handle_.handle(),
            plain_status_callback<doca_dma_task_memcpy_as_task>,
            plain_status_callback<doca_dma_task_memcpy_as_task>,
            max_tasks
        ));
    }

    auto dma_context::memcpy(
        buffer const &src,
        buffer &dest
    ) const -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_dma_task_memcpy_alloc_init,
            doca_dma_task_memcpy_as_task
        >(
            engine(),
            handle_.handle(),
            src.handle(),
            dest.handle()
        );
    }
}
