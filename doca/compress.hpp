#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "device.hpp"
#include "logger.hpp"
#include "progress_engine.hpp"
#include "scoped_counter.hpp"
#include "unique_handle.hpp"

#include <doca_compress.h>

#include <functional>

namespace doca {
    /**
     * DOCA device descriptor for compression tasks.
     */
    class compress_device:
        public device
    {
    public:
        compress_device(std::string pci_addr);
        compress_device();

        [[nodiscard]] auto max_buf_size() const -> std::uint64_t;

    private:
        unique_handle<doca_dev> handle_ { doca_dev_close };
    };

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

    template<typename TaskType>
    class generic_compress_task {
    public:
        generic_compress_task(TaskType *task, doca_data task_user_data):
            handle_ { task },
            dst_ { compress_task_helpers<TaskType>::get_dst(task) },
            task_user_data_ { task_user_data }
        {
        }

        generic_compress_task(generic_compress_task const &) = delete;
        generic_compress_task(generic_compress_task &&) = delete;
        generic_compress_task &operator=(generic_compress_task const &) = delete;
        generic_compress_task &operator=(generic_compress_task &&) = delete;

        ~generic_compress_task() {
            doca_task_free(as_task());
        }

        auto &dst() { return dst_; }
        auto status() const { return doca_task_get_status(as_task()); }
        auto crc_cs() const { return compress_task_helpers<TaskType>::get_crc_cs(handle_); }
        auto adler_cs() const { return compress_task_helpers<TaskType>::get_adler_cs(handle_); }
        auto user_data() const { return task_user_data_; }

    private:
        TaskType *handle;

        auto as_task() const {
            return compress_task_helpers<TaskType>::as_task(handle_);
        }

        TaskType *handle_;
        buffer dst_;
        doca_data task_user_data_;
    };

    using compress_task_compress_deflate = generic_compress_task<doca_compress_task_compress_deflate>;
    using compress_task_decompress_deflate = generic_compress_task<doca_compress_task_decompress_deflate>;

    /**
     * Context for compression tasks.
     */
    class base_compress_context:
        public context
    {
    public:
        /**
         * @param dev device on which the submitted tasks will run
         * @param engine engine that processes the completion events
         */
        base_compress_context(
            progress_engine *parent,
            compress_device const &dev,
            std::uint32_t max_tasks = 1
        );
        ~base_compress_context();

        /**
         * Compress the data in src, write the results to dest. Returns immediately; the result of the call is a future that'll be completed
         * when the task finishes. At this point, the compressed data will be in dest.
         *
         * @param src source data buffer
         * @param dest destination data buffer
         * @return a future that will be completed when the tasks completes
         */
        auto compress(buffer const &src, buffer &dest, doca_data task_user_data = { .ptr = nullptr }) -> void {
            submit_task<doca_compress_task_compress_deflate>(src, dest, task_user_data);
        }

        /**
         * Decompress the data in src, write the results to dest. Returns immediately; the result of the call is a future that'll be completed
         * when the task finishes. At this point, the decompressed data will be in dest.
         *
         * @param src source data buffer
         * @param dest destination data buffer
         * @return a future that will be completed when the tasks completes
         */
        auto decompress(buffer const &src, buffer &dest, doca_data task_user_data = { .ptr = nullptr }) -> void {
            submit_task<doca_compress_task_decompress_deflate>(src, dest, task_user_data);
        }

        [[nodiscard]] auto as_ctx() const -> doca_ctx * override;

        auto stop() -> void override;

    protected:
        virtual auto task_completion_compress_deflate([[maybe_unused]] compress_task_compress_deflate &task) -> void {}
        virtual auto task_error_compress_deflate([[maybe_unused]] compress_task_compress_deflate &task) -> void {}
        virtual auto task_completion_decompress_deflate([[maybe_unused]] compress_task_decompress_deflate &task) -> void {}
        virtual auto task_error_decompress_deflate([[maybe_unused]] compress_task_decompress_deflate &task) -> void {}

    private:
        template<typename TaskType>
        auto submit_task(buffer const &src, buffer &dest, doca_data task_user_data) -> void {
            TaskType *compress_task;

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
        }

        auto do_stop_if_able() -> void;

        static auto task_completion_compress_deflate_entry(
            doca_compress_task_compress_deflate *compress_task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;
        
        static auto task_error_compress_deflate_entry(
            doca_compress_task_compress_deflate *compress_task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;
        
        static auto task_completion_decompress_deflate_entry(
            doca_compress_task_decompress_deflate *compress_task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;
        
        static auto task_error_decompress_deflate_entry(
            doca_compress_task_decompress_deflate *compress_task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        unique_handle<doca_compress> handle_ { doca_compress_destroy };
        scoped_counter<> currently_handling_tasks_;
        bool stop_requested_ = false;
    };

    class compress_context;

    struct compress_callbacks {
        using state_changed_callback = std::function<void(
            compress_context &self,
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        )>;

        using compress_completion_callback = std::function<void(
            compress_context &self,
            compress_task_compress_deflate &task
        )>;
        
        using decompress_completion_callback = std::function<void(
            compress_context &self,
            compress_task_decompress_deflate &task
        )>;

        state_changed_callback state_changed = {};
        compress_completion_callback compress_completed = {};
        compress_completion_callback compress_error = {};
        decompress_completion_callback decompress_completed = {};
        decompress_completion_callback decompress_error = {};
    };

    class compress_context:
        public base_compress_context
    {
    public:
        compress_context(
            progress_engine *parent,
            compress_device const &dev,
            compress_callbacks callbacks,
            std::uint32_t max_tasks = 1
        );

    protected:
        auto state_changed(doca_ctx_states prev_state, doca_ctx_states next_state) -> void override;

        auto task_completion_compress_deflate(compress_task_compress_deflate &task) -> void override;
        auto task_error_compress_deflate(compress_task_compress_deflate &task) -> void override;
        auto task_completion_decompress_deflate(compress_task_decompress_deflate &task) -> void override;
        auto task_error_decompress_deflate(compress_task_decompress_deflate &task) -> void override;

    private:
        compress_callbacks callbacks_;
    };
}
