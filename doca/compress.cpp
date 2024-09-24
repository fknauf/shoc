#include "compress.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <doca_compress.h>

namespace doca {
    compress_device::compress_device(std::string pci_addr):
        device(device::find_by_pci_addr(pci_addr, doca_compress_cap_task_compress_deflate_is_supported))
    {
    }

    compress_device::compress_device():
        device(device::find_by_capabilities(doca_compress_cap_task_compress_deflate_is_supported))
    {
    }

    auto compress_device::max_buf_size() const -> std::uint64_t {
        std::uint64_t size;

        enforce_success(doca_compress_cap_task_compress_deflate_get_max_buf_size(as_devinfo(), &size));

        return size;
    }

    compress_context::compress_context(
        progress_engine *parent,
        compress_device const &dev,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        doca_compress *context = nullptr;
        enforce_success(doca_compress_create(dev.handle(), &context));
        handle_.reset(context);

        context::init_state_changed_callback();
        enforce_success(doca_compress_task_compress_deflate_set_conf(
            handle_.handle(),
            &compress_context::task_completion_entry<doca_compress_task_compress_deflate>,
            &compress_context::task_completion_entry<doca_compress_task_compress_deflate>,
            max_tasks
        ));
        enforce_success(doca_compress_task_decompress_deflate_set_conf(
            handle_.handle(),
            &compress_context::task_completion_entry<doca_compress_task_decompress_deflate>,
            &compress_context::task_completion_entry<doca_compress_task_decompress_deflate>,
            max_tasks
        ));
    }

    compress_context::~compress_context() {
    }

    auto compress_context::as_ctx() const -> doca_ctx * {
        return doca_compress_as_ctx(handle_.handle());
    }
}
