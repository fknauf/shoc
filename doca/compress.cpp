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

    base_compress_context::base_compress_context(
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
            &base_compress_context::task_completion_compress_deflate_entry,
            &base_compress_context::task_error_compress_deflate_entry,
            max_tasks
        ));
        enforce_success(doca_compress_task_decompress_deflate_set_conf(
            handle_.handle(),
            &base_compress_context::task_completion_decompress_deflate_entry,
            &base_compress_context::task_error_decompress_deflate_entry,
            max_tasks
        ));
    }

    base_compress_context::~base_compress_context() {
    }

    auto base_compress_context::as_ctx() const -> doca_ctx * {
        return doca_compress_as_ctx(handle_.handle());
    }

    auto base_compress_context::stop() -> void {
        stop_requested_ = true;
        do_stop_if_able();
    }

    auto base_compress_context::do_stop_if_able() -> void {
        if(currently_handling_tasks_.value() == 0) {
            enforce_success(doca_ctx_stop(as_ctx()), { DOCA_SUCCESS, DOCA_ERROR_IN_PROGRESS });
        }
    }

    auto base_compress_context::task_completion_compress_deflate_entry(
        doca_compress_task_compress_deflate *compress_task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto self = static_cast<base_compress_context*>(base_context);

        {
            auto guard = self->currently_handling_tasks_.guard();
            auto task = compress_task_compress_deflate { compress_task, task_user_data };

            self->task_completion_compress_deflate(task);
        }

        if(self->stop_requested_) {
            self->do_stop_if_able();
        }
    }
    
    auto base_compress_context::task_error_compress_deflate_entry(
        doca_compress_task_compress_deflate *compress_task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto self = static_cast<base_compress_context*>(base_context);

        {
            auto guard = self->currently_handling_tasks_.guard();
            auto task = compress_task_compress_deflate { compress_task, task_user_data };

            self->task_error_compress_deflate(task);
        }

        if(self->stop_requested_) {
            self->do_stop_if_able();
        }
    }
    
    auto base_compress_context::task_completion_decompress_deflate_entry(
        doca_compress_task_decompress_deflate *compress_task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto self = static_cast<base_compress_context*>(base_context);

        {
            auto guard = self->currently_handling_tasks_.guard();
            auto task = compress_task_decompress_deflate { compress_task, task_user_data };

            self->task_completion_decompress_deflate(task);
        }

        if(self->stop_requested_) {
            self->do_stop_if_able();
        }
    }
    
    auto base_compress_context::task_error_decompress_deflate_entry(
        doca_compress_task_decompress_deflate *compress_task,
        doca_data task_user_data,
        doca_data ctx_user_data
    ) -> void {
        auto base_context = static_cast<context*>(ctx_user_data.ptr);
        auto self = static_cast<base_compress_context*>(base_context);

        {
            auto guard = self->currently_handling_tasks_.guard();
            auto task = compress_task_decompress_deflate { compress_task, task_user_data };

            self->task_error_decompress_deflate(task);
        }

        if(self->stop_requested_) {
            self->do_stop_if_able();
        }
    }

    compress_context::compress_context(
        progress_engine *parent,
        compress_device const &dev,
        compress_callbacks callbacks,
        std::uint32_t max_tasks
    ):
        base_compress_context { parent, dev, max_tasks },
        callbacks_ { std::move(callbacks) }
    {}

    auto compress_context::state_changed(doca_ctx_states prev_state, doca_ctx_states next_state) -> void {
        if(callbacks_.state_changed) {
            callbacks_.state_changed(*this, prev_state, next_state);
        }
    }

    auto compress_context::task_completion_compress_deflate(compress_task_compress_deflate &task) -> void {
        if(callbacks_.compress_completed) {
            callbacks_.compress_completed(*this, task);
        }
    }

    auto compress_context::task_error_compress_deflate(compress_task_compress_deflate &task) -> void {
        if(callbacks_.compress_error) {
            callbacks_.compress_error(*this, task);
        }
    }

    auto compress_context::task_completion_decompress_deflate(compress_task_decompress_deflate &task) -> void {
        if(callbacks_.decompress_completed) {
            callbacks_.decompress_completed(*this, task);
        }
    }

    auto compress_context::task_error_decompress_deflate(compress_task_decompress_deflate &task) -> void {
        if(callbacks_.decompress_error) {
            callbacks_.decompress_error(*this, task);
        }
    }
}
