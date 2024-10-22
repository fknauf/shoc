#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"
#include "unique_handle.hpp"

#include <doca_compress.h>

#include <functional>

namespace doca {
    // compile-time lookup table for task-specific helper functions
    template<typename TaskType>
    struct compress_task_helpers {};

    template<> struct compress_task_helpers<doca_compress_task_compress_deflate> {
        static auto constexpr as_task      = doca_compress_task_compress_deflate_as_task;
        static auto constexpr get_crc_cs   = doca_compress_task_compress_deflate_get_crc_cs;
        static auto constexpr get_adler_cs = doca_compress_task_compress_deflate_get_adler_cs;
        static auto constexpr alloc_init   = doca_compress_task_compress_deflate_alloc_init;
        static auto constexpr get_src      = doca_compress_task_compress_deflate_get_src;
        static auto constexpr get_dst      = doca_compress_task_compress_deflate_get_dst;
    };

    template<> struct compress_task_helpers<doca_compress_task_decompress_deflate> {
        static auto constexpr as_task      = doca_compress_task_decompress_deflate_as_task;
        static auto constexpr get_crc_cs   = doca_compress_task_decompress_deflate_get_crc_cs;
        static auto constexpr get_adler_cs = doca_compress_task_decompress_deflate_get_adler_cs;
        static auto constexpr alloc_init   = doca_compress_task_decompress_deflate_alloc_init;
        static auto constexpr get_src      = doca_compress_task_decompress_deflate_get_src;
        static auto constexpr get_dst      = doca_compress_task_decompress_deflate_get_dst;
    };

    /**
     * Additional results of a compression operation, i.e. checksums
     */
    struct compress_checksums {
        std::uint32_t crc = 0;
        std::uint32_t adler = 0;

        compress_checksums() = default;

        template<typename TaskType>
        compress_checksums(TaskType *task):
            crc { compress_task_helpers<TaskType>::get_crc_cs(task) },
            adler { compress_task_helpers<TaskType>::get_adler_cs(task) }
        {}
    };

    using compress_awaitable = coro::status_awaitable<compress_checksums>;

    /**
     * Context for compression tasks.
     */
    class compress_context:
        public context
    {
    public:
        /**
         * @param dev device on which the submitted tasks will run
         * @param engine engine that processes the completion events
         */
        compress_context(
            progress_engine *parent,
            device const &dev,
            std::uint32_t max_tasks = 1
        );
        ~compress_context();

        /**
         * Compress the data in src, write the results to dest. Returns immediately; the result of the call is an awaitable that'll be completed
         * when the task finishes. At this point, the compressed data will be in dest.
         *
         * @param src source data buffer
         * @param dest destination data buffer
         * @param checksums optional pointer to a struct that'll accept checksums
         * @return a status awaitable that will be completed when the tasks completes
         */
        auto compress(
            buffer const &src,
            buffer &dest,
            compress_checksums *checksums = nullptr
        ) {
            logger->trace("{} start", __PRETTY_FUNCTION__);
            return submit_task<doca_compress_task_compress_deflate>(src, dest, checksums);
        }

        /**
         * Decompress the data in src, write the results to dest. Returns immediately; the result of the call is an awaitable that'll be completed
         * when the task finishes. At this point, the decompressed data will be in dest.
         *
         * @param src source data buffer
         * @param dest destination data buffer
         * @param checksums optional pointer to a struct that'll accept checksums
         * @return a status awaitable that will be completed when the tasks completes
         */
        auto decompress(
            buffer const &src,
            buffer &dest,
            compress_checksums *checksums = nullptr
        ) {
            logger->trace("{} start", __PRETTY_FUNCTION__);
            return submit_task<doca_compress_task_decompress_deflate>(src, dest, checksums);
        }

        [[nodiscard]] auto as_ctx() const noexcept -> doca_ctx * override;

        // auto stop() -> void override;

    private:
        template<typename TaskType>
        auto submit_task(
            buffer src,
            buffer dest,
            compress_checksums *checksums
        ) -> compress_awaitable {
            return detail::status_offload<
                compress_task_helpers<TaskType>::alloc_init,
                compress_task_helpers<TaskType>::as_task
            >(
                engine(),
                compress_awaitable::create_space(checksums),
                handle_.handle(),
                src.handle(),
                dest.handle()
            );
        }

        template<typename TaskType>
        static auto task_completion_callback(
            TaskType *compress_task,
            doca_data task_user_data,
            [[maybe_unused]] doca_data ctx_user_data
        ) -> void {
            auto dest = static_cast<compress_awaitable::payload_type*>(task_user_data.ptr);
            auto base_task = compress_task_helpers<TaskType>::as_task(compress_task);

            auto status = doca_task_get_status(base_task);
            dest->emplace_value(status);

            if(dest->additional_data()) {
                auto checksums = compress_checksums { compress_task };
                dest->additional_data().overwrite(std::move(checksums));
            }

            doca_task_free(compress_task_helpers<TaskType>::as_task(compress_task));

            dest->resume();
        }

        unique_handle<doca_compress, doca_compress_destroy> handle_;
    };
}
