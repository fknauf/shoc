#include "compress.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <doca_compress.h>

namespace doca {
    compress_context::compress_context(
        progress_engine *parent,
        device const &dev,
        std::uint32_t max_tasks
    ):
        context { parent }
    {
        enforce(dev.has_capability(device_capability::compress_deflate), DOCA_ERROR_NOT_SUPPORTED);

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
