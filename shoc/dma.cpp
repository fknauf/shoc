#include "dma.hpp"
#include "error.hpp"

#include <shoc/common/status.hpp>
#include <shoc/progress_engine.hpp>

#include <doca_pe.h>

#include <cassert>

namespace shoc {
    dma_context::dma_context(
        context_parent *parent,
        device dev,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_dma_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce(dev_.has_capability(device_capability::dma), DOCA_ERROR_NOT_SUPPORTED);

        enforce_success(doca_dma_task_memcpy_set_conf(
            handle(),
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
            handle(),
            src.handle(),
            dest.handle()
        );
    }
}
