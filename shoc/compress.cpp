#include "compress.hpp"

#include "error.hpp"
#include "logger.hpp"

#include <doca_compress.h>

namespace shoc {
    compress_context::compress_context(
        progress_engine *parent,
        device dev,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_compress_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce(dev_.has_capability(device_capability::compress_deflate), DOCA_ERROR_NOT_SUPPORTED);

        enforce_success(doca_compress_task_compress_deflate_set_conf(
            handle(),
            &compress_context::task_completion_callback<doca_compress_task_compress_deflate>,
            &compress_context::task_completion_callback<doca_compress_task_compress_deflate>,
            max_tasks
        ));
        enforce_success(doca_compress_task_decompress_deflate_set_conf(
            handle(),
            &compress_context::task_completion_callback<doca_compress_task_decompress_deflate>,
            &compress_context::task_completion_callback<doca_compress_task_decompress_deflate>,
            max_tasks
        ));
    }
}
