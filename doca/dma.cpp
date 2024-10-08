#include "dma.hpp"
#include "error.hpp"

#include <doca_pe.h>

#include <cassert>

namespace doca {
    dma_device::dma_device(std::string const &pci_addr):
        device(device::find_by_pci_addr(pci_addr, doca_dma_cap_task_memcpy_is_supported))
    {
    }

    dma_device::dma_device():
        device(device::find_by_capabilities(doca_dma_cap_task_memcpy_is_supported))
    {
    }

    dma_context::dma_context(
        context_parent *parent,
        dma_device &dev,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        doca_dma *raw_handle;
        enforce_success(doca_dma_create(dev.handle(), &raw_handle));
        handle_.reset(raw_handle);

        init_state_changed_callback();
        enforce_success(doca_dma_task_memcpy_set_conf(
            handle_.handle(),
            plain_status_callback_function<doca_dma_task_memcpy, doca_dma_task_memcpy_as_task>,
            plain_status_callback_function<doca_dma_task_memcpy, doca_dma_task_memcpy_as_task>,
            max_tasks
        ));
    }

    auto dma_context::memcpy(
        buffer const &src,
        buffer &dest
    ) const -> status_awaitable {
        doca_dma_task_memcpy *task = nullptr;

        auto result = status_awaitable::create_space();
        doca_data task_user_data = { .ptr = result.dest.get() };

        enforce_success(doca_dma_task_memcpy_alloc_init(
            handle_.handle(),
            src.handle(),
            dest.handle(),
            task_user_data,
            &task
        ));

        auto base_task = doca_dma_task_memcpy_as_task(task);

        if(
            auto err = doca_task_submit(base_task);
            err != DOCA_SUCCESS && err != DOCA_ERROR_IN_PROGRESS
        ) {
            doca_task_free(base_task);
            throw doca_exception(err);
        }

        return result;
    }
}
