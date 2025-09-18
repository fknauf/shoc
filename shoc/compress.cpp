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
        enforce_success(doca_compress_task_decompress_lz4_block_set_conf(
            handle(),
            &compress_context::task_completion_callback<doca_compress_task_decompress_lz4_block>,
            &compress_context::task_completion_callback<doca_compress_task_decompress_lz4_block>,
            max_tasks
        ));
        enforce_success(doca_compress_task_decompress_lz4_stream_set_conf(
            handle(),
            &compress_context::task_completion_callback<doca_compress_task_decompress_lz4_stream>,
            &compress_context::task_completion_callback<doca_compress_task_decompress_lz4_stream>,
            max_tasks
        ));
    }

    auto compress_context::compress(
        buffer const &src,
        buffer &dest,
        compress_checksums *checksums
    ) -> compress_awaitable {
        return detail::status_offload<
            doca_compress_task_compress_deflate_alloc_init,
            doca_compress_task_compress_deflate_as_task
        >(
            engine(),
            compress_awaitable::create_space(checksums),
            handle(),
            src.handle(),
            dest.handle()
        );
    }

    auto compress_context::decompress(
        buffer const &src,
        buffer &dest,
        compress_checksums *checksums
    ) -> compress_awaitable {
        return detail::status_offload<
            doca_compress_task_decompress_deflate_alloc_init,
            doca_compress_task_decompress_deflate_as_task
        >(
            engine(),
            compress_awaitable::create_space(checksums),
            handle(),
            src.handle(),
            dest.handle()
        );
    }

    auto compress_context::decompress_lz4_block(
        buffer const &src,
        buffer &dest,
        compress_checksums *checksums
    ) -> compress_awaitable {
        return detail::status_offload<
            doca_compress_task_decompress_lz4_block_alloc_init,
            doca_compress_task_decompress_lz4_block_as_task
        >(
            engine(),
            compress_awaitable::create_space(checksums),
            handle(),
            src.handle(),
            dest.handle()
        );
    }

    auto compress_context::decompress_lz4_stream(
        bool has_block_checksum,
        bool are_blocks_independent,
        buffer const &src,
        buffer &dest,
        compress_checksums *checksums
    ) -> compress_awaitable {
        return detail::status_offload<
            doca_compress_task_decompress_lz4_stream_alloc_init,
            doca_compress_task_decompress_lz4_stream_as_task
        >(
            engine(),
            compress_awaitable::create_space(checksums),
            handle(),
            static_cast<std::uint8_t>(has_block_checksum),
            static_cast<std::uint8_t>(are_blocks_independent),
            src.handle(),
            dest.handle()
        );
    }
}
