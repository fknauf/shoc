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
            &compress_context::task_completion_compress_deflate_entry,
            &compress_context::task_error_compress_deflate_entry,
            max_tasks
        ));
        enforce_success(doca_compress_task_decompress_deflate_set_conf(
            handle_.handle(),
            &compress_context::task_completion_decompress_deflate_entry,
            &compress_context::task_error_decompress_deflate_entry,
            max_tasks
        ));
    }

    compress_context::~compress_context() {
    }

    auto compress_context::as_ctx() const -> doca_ctx * {
        return doca_compress_as_ctx(handle_.handle());
    }

    auto compress_context::task_completion_compress_deflate_entry(
        doca_compress_task_compress_deflate *compress_task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        logger->trace(__PRETTY_FUNCTION__);

        auto dest = static_cast<coro::receptable<compress_result>*>(task_user_data.ptr);

        dest->value = compress_result { compress_task };
        doca_task_free(compress_task_helpers<doca_compress_task_compress_deflate>::as_task(compress_task));

        dest->coro_handle.resume();
    }

    auto compress_context::task_error_compress_deflate_entry(
        doca_compress_task_compress_deflate *compress_task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        logger->trace(__PRETTY_FUNCTION__);

        auto dest = static_cast<coro::receptable<compress_result>*>(task_user_data.ptr);

        dest->value = compress_result { compress_task };
        doca_task_free(compress_task_helpers<doca_compress_task_compress_deflate>::as_task(compress_task));

        dest->coro_handle.resume();
    }

    auto compress_context::task_completion_decompress_deflate_entry(
        doca_compress_task_decompress_deflate *compress_task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        logger->trace(__PRETTY_FUNCTION__);

        auto dest = static_cast<coro::receptable<compress_result>*>(task_user_data.ptr);

        dest->value = compress_result { compress_task };
        doca_task_free(compress_task_helpers<doca_compress_task_decompress_deflate>::as_task(compress_task));

        dest->coro_handle.resume();
    }

    auto compress_context::task_error_decompress_deflate_entry(
        doca_compress_task_decompress_deflate *compress_task,
        doca_data task_user_data,
        [[maybe_unused]] doca_data ctx_user_data
    ) -> void {
        logger->trace(__PRETTY_FUNCTION__);

        auto dest = static_cast<coro::receptable<compress_result>*>(task_user_data.ptr);

        dest->value = compress_result { compress_task };
        doca_task_free(compress_task_helpers<doca_compress_task_decompress_deflate>::as_task(compress_task));

        dest->coro_handle.resume();
    }
}
