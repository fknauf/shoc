#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"
#include "progress_engine.hpp"
#include "unique_handle.hpp"

#include <doca_erasure_coding.h>

#include <string>

/**
 * Erasure coding functionality, see https://docs.nvidia.com/doca/sdk/doca+erasure+coding/index.html
 */
namespace shoc {
    class ec_context;

    /**
     * Encoding matrix to create redundancy blocks. Needed to create
     * corresponding update/recover matrices.
     */
    class ec_coding_matrix {
    public:
        friend class ec_context;

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        ec_coding_matrix(
            ec_context const &ctx,
            doca_ec_matrix_type type,
            std::size_t data_block_count,
            std::size_t rdnc_block_count
        );

        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    /**
     * Recovery matrix to restore missing data blocks. Requires the encoding
     * matrix used for available blocks and info about which blocks are missing.
     */
    class ec_recover_matrix {
    public:
        friend class ec_context;

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        ec_recover_matrix(
            ec_context const &ctx,
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t const> missing_indices
        );

        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    /**
     * Update matrix to recalculate redundancy blocks when data blocks change.
     * Requires the encoding matrix used to calculate existing redundancy blocks
     * and info about wich blocks have changed.
     */
    class ec_update_matrix {
    public:
        friend class ec_context;

        [[nodiscard]]
        auto handle() const noexcept { return handle_.get(); }

    private:
        ec_update_matrix(
            ec_context const &ctx,
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t const> update_indices
        );

        unique_handle<doca_ec_matrix, doca_ec_matrix_destroy> handle_;
    };

    /**
     * Offloading context for erasure coding.
     * 
     * Used to calculate a number of redundancy blocks such that the payload data can be
     * recovered even when up to the number of redundancy blocks go missing. Similar to RAID-6.
     * 
     * Non-obvious things to know when using this context:
     * 
     * - Data blocks must be a multiple of 64 bytes large.
     * - For optimal performance they should be aligned to 64B boundaries (use shoc::aligned_blocks)
     * - During recovery, the source data buffer must be as large as the payload data,
     *   i.e. blocksize * data_block_count bytes. If more than the required number of
     *   redundancy blocks is available, the superfluous ones must be left out, or DOCA
     *   will return an I/O error because the buffer does not match the recovery matrix.
     * - Redundancy blocks are numbered consecutively with payload blocks, e.g. if an encoding
     *   has data blocks 0-4, the first redundancy block will have index 5
     */
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

        [[nodiscard]] static auto create(
            progress_engine_lease &engine,
            device dev,
            std::uint32_t max_tasks = 16
        ) {
            return engine.create_context<ec_context>(std::move(dev), max_tasks);
        }

        /**
         * Create an erasure encoding for the provided data
         * 
         * @param coding_matrix [in] EC encoding matrix
         * @param original_data_blocks [in] payload data for which redundancy blocks need to be calculated
         * @param rdnc_blocks [out] destination buffer for the redundancy blocks
         */
        auto create(
            ec_coding_matrix const &coding_matrix,
            buffer const &original_data_blocks,
            buffer &rdnc_blocks
        ) -> coro::status_awaitable<>;

        /**
         * Recover missing data blocks
         * 
         * Available data blocks need to be written into the available_blocks buffer in ascending
         * order, skipping missing blocks. The number of provided available payload/redundancy
         * blocks must equal the number of original payload blocks so that their geometry matches
         * the recovery matrix. if mor than the required number of redundancy blocks are available,
         * leave out the superfluous ones.
         * 
         * E.g., in a scenario with 4 payload blocks and 3 redundancy blocks, if blocks 1 and 3
         * are missing, available_blocks should conatain the concatenation of payload blocks
         * 0 and 2 and the first two redundancy blocks.
         * 
         * @param recover_matrix [in] EC recovery matrix
         * @param available_blocks [in] Available data and redundancy blocks
         * @param recovered_data_blocks [out] Destinatin buffer for recovered data blocks
         */
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

        /**
         * Create an encoding matrix for a given EC geometry
         * 
         * @param type Type of the Encoding (DOCA_EC_MATRIX_TYPE_CAUCHY or DOCA_EC_MATRIX_TYPE_VANDERMONDE)
         * @param data_block_count number of payload data blocks
         * @param rdnc_block_count number of redundancy blocks
         */
        [[nodiscard]]
        auto coding_matrix(
            doca_ec_matrix_type type,
            std::size_t data_block_count,
            std::size_t rdnc_block_count
        ) const {
            return ec_coding_matrix(*this, type, data_block_count, rdnc_block_count);
        }

        [[nodiscard]]
        auto update_matrix(
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t const> update_indices
        ) const {
            return ec_update_matrix(*this, coding_matrix, update_indices);
        }

        /**
         * Create a recovery matrix.
         * 
         * @param coding_matrix The encoding matrix of the EC, provided by ec_context::coding_matrix
         * @param missing_indices List of indices of missing payload/redundancy blocks.
         */
        [[nodiscard]]
        auto recover_matrix(
            ec_coding_matrix const &coding_matrix,
            std::span<std::uint32_t const> missing_indices
        ) const {
            return ec_recover_matrix(*this, coding_matrix, missing_indices);
        }

    private:
        device dev_;
    };
}
