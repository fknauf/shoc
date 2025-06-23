#pragma once

#include <cstdint>
#include <doca_types.h>
#include <span>

namespace shoc {
    enum class icmp_type : std::uint8_t {
        echo_reply = 0,
        destination_unreachable = 3,
        source_quench = 4,
        redirect_message = 5,
        echo_request = 8,
        router_advertisement = 9,
        router_solicitation = 10,
        time_exceeded = 11,
        parameter_problem = 12,
        timestamp = 13,
        timestamp_reply = 14,
        extended_echo_request = 42,
        extended_echo_reply = 43
    };

    struct icmp_header {
        std::uint8_t type;
        std::uint8_t code;
        doca_be_16_t checksum;
        doca_be_32_t rest_of_header;
    } __attribute__((packed));

    struct tcp_segment {
        doca_be_16_t source_port;
        doca_be_16_t destination_port;
        doca_be_32_t sequence_number;
        doca_be_32_t ack_number;

        std::uint8_t data_offset : 4;
        std::uint8_t reserved : 4;
        std::uint8_t flags;
        doca_be_16_t window;
        doca_be_16_t checksum;
        doca_be_16_t urgent_ptr;
        doca_be32_t options_words[];

        auto options()       -> std::span<doca_be32_t>       { return { options_words, data_offset - 5 }; }
        auto options() const -> std::span<doca_be32_t const> { return { options_words, data_offset - 5 }; }

        auto data() -> std::span<std::byte> { return { } }
    } __attribute__((packed));

    struct ipv4_packet {
        std::uint8_t version : 4;
        std::uint8_t ihl : 4;
        std::uint8_t dscp : 6;
        std::uint8_t ecn : 2;
        doca_be16_t total_length;

        doca_be16_t identification;
        doca_be16_t flags : 3;
        doca_be16_t fragment_offset : 13;

        std::uint8_t ttl;
        std::uint8_t protocol;
        doca_be16_t header_checksum;

        doca_be32_t source_address;
        doca_be32_t destination_address;

        doca_be32_t options_words[];

        auto options()       -> std::span<doca_be32_t>       { return { options_words, ihl - 5 }; }
        auto options() const -> std::span<doca_be32_t const> { return { options_words, ihl - 5 }; }

        auto payload() -> std::span<std::byte> {
            return { reinterpret_cast<std::byte*>(options_words + ihl - 5), total_length - ihl * 4 };
        }

        auto payload() const -> std::span<std::byte const> {
            return { reinterpret_cast<std::byte const*>(options_words + ihl - 5), total_length - ihl * 4 };
        }
    } __attribute__((packed));

    struct ipv6_packet {
        doca_be32_t version : 4;
        doca_be32_t traffic_class : 8;
        doca_be32_t flow_label: 20;

        doca_be16_t payload_length;
        std::uint8_t next_header;
        std::uint8_t hop_limit;
        
        std::byte source_addres[16];
        std::byte destination_address[16];

        std::byte payload[];
    } __attribute__((packed));

    struct eth_frame {
        std::byte mac_dest[6];
        std::byte mac_src[6];
        std::uint16_t ethertype;
    
        union {
            ipv4_packet ipv4;
            ipv6_packet ipv6;
        }
    } __attribute__((packed));
}
