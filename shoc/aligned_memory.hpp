#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace shoc {
    /**
     * Many operations in DOCA will perform better when they are performed on cache-line-aligned data.
     * This class provides aligned memory, by default aligned on 64 bytes (i.e., cache lines)
     *
     * This is built on top of std::vector, so the actual memory will be on the heap, and the anchor
     * object is moveable.
     * 
     * TODO: Add hugepages support
     */
    class aligned_memory {
    public:
        aligned_memory() = default;

        /**
         * @param size size of the alligned memory
         * @param alignment in bytes
         */
        aligned_memory(std::size_t size, std::size_t alignment = 64):
            memory_ { size + alignment }
        {
            auto base_ptr = static_cast<void*>(memory_.data());
            auto space = memory_.size();
            std::align(alignment, size, base_ptr, space);

            aligned_ = std::span { static_cast<std::byte*>(base_ptr), size };
        }

        aligned_memory(aligned_memory const &) = delete;
        aligned_memory(aligned_memory &&other) {
            swap(other);
        }

        aligned_memory &operator=(aligned_memory const &) = delete;
        aligned_memory &operator=(aligned_memory &&other) {
            auto temp = aligned_memory(std::move(other));
            swap(temp);
            return *this;
        }

        auto swap(aligned_memory &other) -> void {
            using std::swap;

            swap(memory_, other.memory_);
            swap(aligned_, other.aligned_);
        }

        /**
         * @return the aligned memory as constant byte span
         */
        auto as_bytes() const -> std::span<std::byte const> {
            return aligned_;
        }

        /**
         * @return the aligned memory as mutable byte span
         */
        auto as_writable_bytes() const -> std::span<std::byte> {
            return aligned_;
        }

        /**
         * Assign data to the memory. If fewer bytes are assigned than the buffer can
         * hold, the rest will be set to zero.
         *
         * @param data source data bytes
         */
        auto assign(std::span<std::byte const> data) -> void {
            assert(data.size() < aligned_.size());

            auto last = std::copy(data.begin(), data.end(), aligned_.begin());
            std::fill(last, aligned_.end(), std::byte{});
        }

    private:
        std::vector<std::byte> memory_;
        std::span<std::byte> aligned_;
    };

    inline auto swap(aligned_memory &lhs, aligned_memory &rhs) -> void {
        lhs.swap(rhs);
    }

    /**
     * Often we need not just a plain memory buffer but a series of identically sized and aligned
     * blocks for batched tasks. This class provides that.
     */
    class aligned_blocks {
    public:
        aligned_blocks() = default;

        /**
         * @param block_count number of blocks
         * @param block_size size of each block, must be a multiple of alignment
         * @param alignment alignment in bytes, cache-line-aligned by default
         */
        aligned_blocks(
            std::size_t block_count,
            std::size_t block_size,
            std::size_t alignment = 64
        ):
            memory_ { block_count * block_size, alignment },
            block_count_ { block_count },
            block_size_ { block_size }
        {
            assert(block_size % alignment == 0);
        }

        auto block_count() const { return block_count_; };
        auto block_size() const { return block_size_; };

        /**
         * @return the index-th block as a constant byte span
         */
        auto block(std::size_t index) const {
            auto offset = index * block_size_;
            return memory_.as_bytes().subspan(offset, block_size_);
        }

        /**
         * @return the index-th block as a mutable byte span
         */
        auto writable_block(std::size_t index) {
            auto offset = index * block_size_;
            return memory_.as_writable_bytes().subspan(offset, block_size_);
        }

        /**
         * @return the whole memory buffer as constant byte span
         */
        auto as_bytes() const {
            return memory_.as_bytes();
        }

        /**
         * @return the whole memory buffer as mutable byte span
         */
        auto as_writable_bytes() {
            return memory_.as_writable_bytes();
        }

        /**
         * Assign data to the whole underlying memory buffer. The assigned data is
         * implicitly partitioned into blocks.
         */
        auto assign(std::span<std::byte const> data) -> void {
            memory_.assign(data);
        }

        auto assign(std::span<char const> data) -> void {
            memory_.assign(std::as_bytes(data));
        }

    private:
        aligned_memory memory_;
        std::size_t block_count_ = 0;
        std::size_t block_size_ = 0;
    };
}
