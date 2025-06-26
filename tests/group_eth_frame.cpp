#include <shoc/eth_frame.hpp>

#include <gtest/gtest.h>

#include <cppcodec/hex_lower.hpp>

TEST(docapp_eth_frame, ipv4_udp) {
    auto buffer = cppcodec::hex_lower::decode<std::vector<std::uint8_t>>("02d1cf1110511070fdb33a0f08004500002122d240004011cdcdc0a86401c0a864da8dc33039000d5749663030310a00000000000000000000000000");
    auto frame = reinterpret_cast<shoc::eth_frame*>(buffer.data());

    ASSERT_EQ(frame->destination_mac().size(), 6);
    EXPECT_EQ(frame->destination_mac()[0], std::byte{0x02});
    EXPECT_EQ(frame->destination_mac()[1], std::byte{0xd1});
    EXPECT_EQ(frame->destination_mac()[2], std::byte{0xcf});
    EXPECT_EQ(frame->destination_mac()[3], std::byte{0x11});
    EXPECT_EQ(frame->destination_mac()[4], std::byte{0x10});
    EXPECT_EQ(frame->destination_mac()[5], std::byte{0x51});

    ASSERT_EQ(frame->source_mac().size(), 6);
    EXPECT_EQ(frame->source_mac()[0], std::byte{0x10});
    EXPECT_EQ(frame->source_mac()[1], std::byte{0x70});
    EXPECT_EQ(frame->source_mac()[2], std::byte{0xfd});
    EXPECT_EQ(frame->source_mac()[3], std::byte{0xb3});
    EXPECT_EQ(frame->source_mac()[4], std::byte{0x3a});
    EXPECT_EQ(frame->source_mac()[5], std::byte{0x0f});

    EXPECT_EQ(frame->ethertype(), 0x0800);

    auto packet = frame->ipv4_payload();

    EXPECT_EQ(packet->version(), 4);
    EXPECT_EQ(packet->ihl(), 5);
    EXPECT_EQ(packet->dscp(), 0);
    EXPECT_EQ(packet->ecn(), 0);
    EXPECT_EQ(packet->total_length(), 0x0021);
    EXPECT_EQ(packet->identification(), 0x22d2);
    EXPECT_EQ(packet->flags(), 2);
    EXPECT_EQ(packet->fragment_offset(), 0);
    EXPECT_EQ(packet->ttl(), 0x40);
    EXPECT_EQ(packet->protocol(), 0x11);
    EXPECT_EQ(packet->header_checksum(), 0xcdcd);
    EXPECT_EQ(packet->header_checksum(), packet->calculate_header_checksum());
    EXPECT_EQ(packet->source_address(), 0xc0a86401);
    EXPECT_EQ(packet->destination_address(), 0xc0a864da);

    EXPECT_EQ(packet->options().size(), 0);

    auto segment = packet->udp_payload();

    EXPECT_EQ(segment->source_port(), 0x8dc3);
    EXPECT_EQ(segment->destination_port(), 0x3039);
    EXPECT_EQ(segment->length(), 13);
    EXPECT_EQ(segment->checksum(), 0x5749);

    auto data = segment->data();

    ASSERT_EQ(data.size(), 5);
    EXPECT_EQ(data[0], std::byte{0x66});
    EXPECT_EQ(data[1], std::byte{0x30});
    EXPECT_EQ(data[2], std::byte{0x30});
    EXPECT_EQ(data[3], std::byte{0x31});
    EXPECT_EQ(data[4], std::byte{0x0a});

    segment->source_port(0x3039)->destination_port(0xdde5);

    EXPECT_EQ(segment->source_port(), 0x3039);
    EXPECT_EQ(segment->destination_port(), 0xdde5);

    packet
        ->version(6)
        ->ihl(6)
        ->dscp(42)
        ->ecn(1)
        ->total_length(20)
        ->identification(0x1234)
        ->flags(3)
        ->fragment_offset(42)
        ->ttl(10)
        ->protocol(123)
        ->header_checksum(0)
        ->source_address(0x12345678)
        ->destination_address(0x87654321);

    EXPECT_EQ(packet->version(), 6);
    EXPECT_EQ(packet->ihl(), 6);
    EXPECT_EQ(packet->dscp(), 42);
    EXPECT_EQ(packet->ecn(), 1);
    EXPECT_EQ(packet->total_length(), 20);
    EXPECT_EQ(packet->identification(), 0x1234);
    EXPECT_EQ(packet->flags(), 3);
    EXPECT_EQ(packet->fragment_offset(), 42);
    EXPECT_EQ(packet->ttl(), 10);
    EXPECT_EQ(packet->protocol(), 123);
    EXPECT_EQ(packet->header_checksum(), 0);
    EXPECT_EQ(packet->source_address(), 0x12345678);
    EXPECT_EQ(packet->destination_address(), 0x87654321);
}
