#include <shoc/aligned_memory.hpp>
#include <boost/cobalt.hpp>
#include <gtest/gtest.h>

namespace {
    auto is_aligned(std::span<std::byte const> mem) -> bool {
        return reinterpret_cast<std::uintptr_t>(mem.data()) % 64 == 0;
    }
}

TEST(aligned_mem, basic_operation) {
    auto mem = shoc::aligned_memory { 1024 };

    EXPECT_EQ(mem.as_bytes().size(), 1024);
    EXPECT_NE(mem.as_bytes().data(), nullptr);
    EXPECT_TRUE(is_aligned(mem.as_bytes()));

    auto mem2 = std::move(mem);

    EXPECT_EQ(mem.as_bytes().size(), 0);
    EXPECT_EQ(mem2.as_bytes().size(), 1024);
    EXPECT_NE(mem2.as_bytes().data(), nullptr);
    EXPECT_TRUE(is_aligned(mem2.as_bytes()));

    mem = std::move(mem2);

    EXPECT_EQ(mem2.as_bytes().size(), 0);
    EXPECT_EQ(mem.as_bytes().size(), 1024);
    EXPECT_NE(mem.as_bytes().data(), nullptr);
    EXPECT_TRUE(is_aligned(mem.as_bytes()));

    swap(mem, mem2);

    EXPECT_EQ(mem.as_bytes().size(), 0);
    EXPECT_EQ(mem2.as_bytes().size(), 1024);
    EXPECT_NE(mem2.as_bytes().data(), nullptr);
    EXPECT_TRUE(is_aligned(mem2.as_bytes()));
}

TEST(aligned_blocks, basic_operation) {
    auto blocks = shoc::aligned_blocks(4, 1024);

    EXPECT_EQ(blocks.block_count(), 4);
    EXPECT_EQ(blocks.block_size(), 1024);
    EXPECT_NE(blocks.block(0).data(), nullptr);
    EXPECT_EQ(blocks.block(0).size(), 1024);
    EXPECT_TRUE(is_aligned(blocks.block(0)));
}