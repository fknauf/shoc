#include "sha.hpp"

#include "progress_engine.hpp"

namespace shoc {
    sha_context::sha_context(
        context_parent *parent,
        device dev,
        std::uint32_t max_tasks
    ):
        context { 
            parent,
            context::create_doca_handle<doca_sha_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce(dev_.has_capability(device_capability::sha), DOCA_ERROR_NOT_SUPPORTED);

        enforce_success(doca_sha_task_hash_set_conf(
            handle(),
            plain_status_callback<doca_sha_task_hash_as_task>,
            plain_status_callback<doca_sha_task_hash_as_task>,
            max_tasks
        ));

        enforce_success(doca_sha_task_partial_hash_set_conf(
            handle(),
            plain_status_callback<doca_sha_task_partial_hash_as_task>,
            plain_status_callback<doca_sha_task_partial_hash_as_task>,
            max_tasks
        ));
    }

    auto sha_context::hash(
        doca_sha_algorithm algorithm,
        buffer const &src_buf,
        buffer &dst_buf
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_sha_task_hash_alloc_init,
            doca_sha_task_hash_as_task
        >(
            engine(),
            handle(),
            algorithm,
            src_buf.handle(),
            dst_buf.handle()
        );        
    }

    auto sha_context::partial_hash(
        doca_sha_algorithm algorithm,
        buffer const &src_buf,
        buffer &dst_buf,
        bool final_segment
    ) -> coro::status_awaitable<> {
        auto allocate_partial_hash_task = [](
            doca_sha *sha,
            doca_sha_algorithm algorithm,
            doca_buf const *src_buf,
            doca_buf *dst_buf,
            bool final_segment,
            doca_data user_data,
            doca_sha_task_partial_hash **task
        ) -> doca_error_t {
            auto err = doca_sha_task_partial_hash_alloc_init(
                sha, algorithm, src_buf, dst_buf, user_data, task
            );

            if(!final_segment || err != DOCA_SUCCESS) {
                return err;
            }

            return doca_sha_task_partial_hash_set_is_final_buf(*task);
        };

        return detail::plain_status_offload<
            allocate_partial_hash_task,
            doca_sha_task_partial_hash_as_task
        >(
            engine(),
            handle(),
            algorithm,
            src_buf.handle(),
            dst_buf.handle(),
            final_segment
        );        
    }
}
