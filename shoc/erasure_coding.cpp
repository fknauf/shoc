#include "erasure_coding.hpp"

#include "progress_engine.hpp"

namespace shoc {
    ec_coding_matrix::ec_coding_matrix(
        ec_context const &ctx,
        doca_ec_matrix_type type,
        std::size_t data_block_count,
        std::size_t rdnc_block_count
    ) {
        doca_ec_matrix *matrix;

        enforce_success(doca_ec_matrix_create(
            ctx.handle(),
            type,
            data_block_count,
            rdnc_block_count,
            &matrix
        ));

        handle_.reset(matrix);
    }

    ec_update_matrix::ec_update_matrix(
        ec_context const &ctx,
        ec_coding_matrix const &coding_matrix,
        std::span<std::uint32_t const> update_indices
    ) {
        doca_ec_matrix *matrix;

        enforce_success(doca_ec_matrix_create_update(
            ctx.handle(),
            coding_matrix.handle(),
            const_cast<std::uint32_t*>(update_indices.data()),
            update_indices.size(),
            &matrix
        ));

        handle_.reset(matrix);
    }

    ec_recover_matrix::ec_recover_matrix(
        ec_context const &ctx,
        ec_coding_matrix const &coding_matrix,
        std::span<std::uint32_t const> missing_indices
    ) {
        doca_ec_matrix *matrix;

        enforce_success(doca_ec_matrix_create_recover(
            ctx.handle(),
            coding_matrix.handle(),
            const_cast<std::uint32_t*>(missing_indices.data()),
            missing_indices.size(),
            &matrix
        ));

        handle_.reset(matrix);
    }

    ec_context::ec_context(
        context_parent *parent,
        device dev,
        std::uint32_t max_tasks
    ):
        context {
            parent,
            context::create_doca_handle<doca_ec_create>(dev.handle())
        },
        dev_ { std::move(dev) }
    {
        enforce(dev_.has_capability(device_capability::erasure_coding), DOCA_ERROR_NOT_SUPPORTED);

        enforce_success(doca_ec_task_create_set_conf(
            handle(),
            plain_status_callback<doca_ec_task_create_as_task>,
            plain_status_callback<doca_ec_task_create_as_task>,
            max_tasks
        ));

        enforce_success(doca_ec_task_update_set_conf(
            handle(),
            plain_status_callback<doca_ec_task_update_as_task>,
            plain_status_callback<doca_ec_task_update_as_task>,
            max_tasks
        ));

        enforce_success(doca_ec_task_recover_set_conf(
            handle(),
            plain_status_callback<doca_ec_task_recover_as_task>,
            plain_status_callback<doca_ec_task_recover_as_task>,
            max_tasks
        ));
    }

    auto ec_context::create(
        ec_coding_matrix const &coding_matrix,
        buffer const &original_data_blocks,
        buffer &rdnc_blocks
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_ec_task_create_allocate_init,
            doca_ec_task_create_as_task
        >(
            engine(),
            handle(),
            coding_matrix.handle(),
            original_data_blocks.handle(),
            rdnc_blocks.handle()
        );
    }

    auto ec_context::recover(
        ec_recover_matrix const &recover_matrix,
        buffer const &available_blocks,
        buffer &recovered_data_blocks
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_ec_task_recover_allocate_init,
            doca_ec_task_recover_as_task
        >(
            engine(),
            handle(),
            recover_matrix.handle(),
            available_blocks.handle(),
            recovered_data_blocks.handle()
        );
    }

    auto ec_context::update(
        ec_update_matrix const &update_matrix,
        buffer const &original_updated_and_rdnc_blocks,
        buffer &updated_rdnc_blocks
    ) -> coro::status_awaitable<> {
        return detail::plain_status_offload<
            doca_ec_task_update_allocate_init,
            doca_ec_task_update_as_task
        >(
            engine(),
            handle(),
            update_matrix.handle(),
            original_updated_and_rdnc_blocks.handle(),
            updated_rdnc_blocks.handle()
        );
    }
}
