#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"

#include "unique_handle.hpp"

#include <doca_erasure_coding.h>

#include <string>

namespace shoc {
    class ec_context;

    class ec_coding_matrix {
    public:
        ec_coding_matrix(
            ec_context const &ctx,
            doca_ec_matrix_type type,
            std::size_t data_block_count,
            std::size_t rdnc_block_count
        );

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    class ec_recover_matrix {
    public:
        ec_recover_matrix(
            ec_context const &ctx,
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t> missing_indices
        );
    
        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    class ec_update_matrix {
    public:
        ec_update_matrix(
            ec_context const &ctx,
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t> update_indices
        );

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    class ec_context:
        public context<
            doca_ec,
            doca_ec_destroy,
            doca_ec_as_ctx
        >
    {
    public:
        ec_context(
            context_parent *parent,
            device dev,
            std::uint32_t max_tasks = 16
        );

        auto create(
            ec_coding_matrix const &coding_matrix,
            buffer const &original_data_blocks,
            buffer & rdnc_blocks
        ) -> coro::status_awaitable<>;

        auto recover(
            ec_recover_matrix const &recover_matrix,
            buffer const &available_blocks,
            buffer &recovered_data_blocks
        ) -> coro::status_awaitable<>;

        auto update(
            ec_update_matrix const &update_matrix,
            buffer const &original_updated_and_rdnc_blocks,
            buffer &updated_rdnc_blocks
        ) -> coro::status_awaitable<>;

        [[nodiscard]]
        auto coding_matrix(doca_ec_matrix_type type,
            std::size_t data_block_count,
            std::size_t rdnc_block_count
        ) const {
            return ec_coding_matrix(*this, type, data_block_count, rdnc_block_count);
        }

        [[nodiscard]]
        auto update_matrix(
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t> update_indices
        ) const {
            return ec_update_matrix(*this, coding_matrix, update_indices);
        }

        [[nodiscard]]
        auto recover_matrix(
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t> missing_indices
        ) const {
            return ec_recover_matrix(*this, coding_matrix, missing_indices);
        }

    private:
        device dev_;
    }; 
}