#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/value_awaitable.hpp"
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
     * Result of a compression operation: compressed data, checksums, status
     */
    class compress_result {
    public:
        compress_result() = default;

        template<typename TaskType>
        compress_result(TaskType *task):
            status_ { doca_task_get_status(compress_task_helpers<TaskType>::as_task(task)) },
            crc_cs_ { compress_task_helpers<TaskType>::get_crc_cs(task) },
            adler_cs_ { compress_task_helpers<TaskType>::get_adler_cs(task) }
        {
        }

        auto status() const { return status_; }
        auto crc_cs() const { return crc_cs_; }
        auto adler_cs() const { return adler_cs_; }

    private:
        std::uint32_t status_ = DOCA_ERROR_EMPTY;
        std::uint32_t crc_cs_ = 0;
        std::uint32_t adler_cs_ = 0;
    };

    using compress_awaitable = coro::value_awaitable<compress_result>;

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
         * @return a future that will be completed when the tasks completes
         */
        auto compress(buffer const &src, buffer &dest) {
            logger->trace("{} start", __PRETTY_FUNCTION__);
            return submit_task<doca_compress_task_compress_deflate>(src, dest);
        }

        /**
         * Decompress the data in src, write the results to dest. Returns immediately; the result of the call is an awaitable that'll be completed
         * when the task finishes. At this point, the decompressed data will be in dest.
         *
         * @param src source data buffer
         * @param dest destination data buffer
         * @return a future that will be completed when the tasks completes
         */
        auto decompress(buffer const &src, buffer &dest) {
            logger->trace("{} start", __PRETTY_FUNCTION__);
            return submit_task<doca_compress_task_decompress_deflate>(src, dest);
        }

        [[nodiscard]] auto as_ctx() const noexcept -> doca_ctx * override;

        // auto stop() -> void override;

    private:
        template<typename TaskType>
        auto submit_task(buffer src, buffer dest) -> compress_awaitable {
            logger->trace(__PRETTY_FUNCTION__);

            auto result = compress_awaitable::create_space();

            TaskType *compress_task;
            doca_data task_user_data = {
                .ptr = result.dest.get()
            };

            logger->trace("{} dest = {}", __PRETTY_FUNCTION__, task_user_data.ptr);

            enforce_success(compress_task_helpers<TaskType>::alloc_init(
                handle_.handle(),
                src.handle(),
                dest.handle(),
                task_user_data,
                &compress_task
            ));

            auto base_task = compress_task_helpers<TaskType>::as_task(compress_task);
            auto err = doca_task_submit(base_task);

            if(err != DOCA_SUCCESS) {
                doca_task_free(base_task);
                throw doca_exception(err);
            } else {
                doca_buf_inc_refcount(dest.handle(), nullptr);
            }

            return result;
        }

        template<typename TaskType>
        static auto task_completion_callback(
            TaskType *compress_task,
            doca_data task_user_data,
            [[maybe_unused]] doca_data ctx_user_data
        ) -> void {
            auto dest = static_cast<compress_awaitable::payload_type*>(task_user_data.ptr);

            dest->value = compress_result { compress_task };
            doca_task_free(compress_task_helpers<TaskType>::as_task(compress_task));

            if(dest->coro_handle) {
                dest->coro_handle.resume();
            }
        }

        unique_handle<doca_compress> handle_ { doca_compress_destroy };
    };
}
