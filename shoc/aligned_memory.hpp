#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace shoc {
    class aligned_memory {
    public:
        aligned_memory() = default;
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

        auto as_bytes() const -> std::span<std::byte const> {
            return aligned_;
        }

        auto as_writable_bytes() const -> std::span<std::byte> {
            return aligned_;
        }

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

    class aligned_blocks {
    public:
        aligned_blocks() = default;

        aligned_blocks(std::size_t block_count, std::size_t block_size):
            memory_ { block_count * block_size },
            block_count_ { block_count },
            block_size_ { block_size }
        {}

        auto block_count() const { return block_count_; };
        auto block_size() const { return block_size_; };

        auto block(std::size_t index) const {
            auto offset = index * block_size_;
            return memory_.as_bytes().subspan(offset, block_size_);
        }

        auto writable_block(std::size_t index) {
            auto offset = index * block_size_;
            return memory_.as_writable_bytes().subspan(offset, block_size_);
        }

        auto as_bytes() const {
            return memory_.as_bytes();
        }

        auto as_writable_bytes() {
            return memory_.as_writable_bytes();
        }

        auto assign(std::span<std::byte const> data) -> void {
            memory_.assign(data);
        }

        auto assign(std::span<char const> data) -> void{
            memory_.assign(std::as_bytes(data));
        }

    private:
        aligned_memory memory_;
        std::size_t block_count_ = 0;
        std::size_t block_size_ = 0;
    };
}
