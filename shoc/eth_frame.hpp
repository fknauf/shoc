#pragma once

#include <doca_types.h>

#include <algorithm>
#include <cstdint>
#include <span>

namespace shoc {
//    enum class icmp_type : std::uint8_t {
//        echo_reply = 0,
//        destination_unreachable = 3,
//        source_quench = 4,
//        redirect_message = 5,
//        echo_request = 8,
//        router_advertisement = 9,
//        router_solicitation = 10,
//        time_exceeded = 11,
//        parameter_problem = 12,
//        timestamp = 13,
//        timestamp_reply = 14,
//        extended_echo_request = 42,
//        extended_echo_reply = 43
//    };
//
//    struct icmp_header {
//        std::uint8_t type;
//        std::uint8_t code;
//        doca_be16_t checksum;
//        doca_be32_t rest_of_header;
//    } __attribute__((packed));
//
//    struct tcp_segment {
//        doca_be16_t source_port;
//        doca_be16_t destination_port;
//        doca_be32_t sequence_number;
//        doca_be32_t ack_number;
//
//        std::uint8_t data_offset : 4;
//        std::uint8_t reserved : 4;
//        std::uint8_t flags;
//        doca_be16_t window;
//        doca_be16_t checksum;
//        doca_be16_t urgent_ptr;
//        doca_be32_t options_words[];
//
//        auto options()       -> std::span<doca_be32_t>       { return { options_words, data_offset - 5 }; }
//        auto options() const -> std::span<doca_be32_t const> { return { options_words, data_offset - 5 }; }
//
//        auto data() -> std::span<std::byte> {
//            auto base_ptr = reinterpret_cast<std::byte*>(this) + data_offset * 4;
//
//            return { base_ptr }
//        }
//    } __attribute__((packed));

    class udp_segment {
    public:
        [[nodiscard]] auto source_port     () const -> std::uint16_t { return be16toh(raw_source_port); }
        [[nodiscard]] auto destination_port() const -> std::uint16_t { return be16toh(raw_destination_port); }
        [[nodiscard]] auto length          () const -> std::uint16_t { return be16toh(raw_length); }
        [[nodiscard]] auto checksum        () const -> std::uint16_t { return be16toh(raw_checksum); }

        auto &source_port     (std::uint16_t value) { raw_source_port      = htobe16(value); return *this; }
        auto &destination_port(std::uint16_t value) { raw_destination_port = htobe16(value); return *this; }
        auto &length          (std::uint16_t value) { raw_length           = htobe16(value); return *this; }
        auto &checksum        (std::uint16_t value) { raw_checksum         = htobe16(value); return *this; }

        auto data() -> std::span<std::byte> {
            return { raw_data, static_cast<std::size_t>(length() - 8) };
        }

        auto data() const -> std::span<std::byte const> {
            return { raw_data, static_cast<std::size_t>(length() - 8) };
        }

    private:
        doca_be16_t raw_source_port;
        doca_be16_t raw_destination_port;
        doca_be16_t raw_length;
        doca_be16_t raw_checksum;
        std::byte   raw_data[0];
    } __attribute__((packed));

    inline auto octets_to_ipv4_addr(
        std::uint8_t a,
        std::uint8_t b,
        std::uint8_t c,
        std::uint8_t d
    ) {
        return 
            static_cast<std::uint32_t>(a) << 24 | 
            static_cast<std::uint32_t>(b) << 16 |
            static_cast<std::uint32_t>(c) << 8 |
            static_cast<std::uint32_t>(d);
    }

    class ipv4_packet {
    public:
        [[nodiscard]] auto version            () const -> std::uint8_t  { return raw_version_ihl >> 4; }
        [[nodiscard]] auto ihl                () const -> std::uint8_t  { return raw_version_ihl & 0x0f; }
        [[nodiscard]] auto dscp               () const -> std::uint8_t  { return raw_dscp_ecn >> 2; }
        [[nodiscard]] auto ecn                () const -> std::uint8_t  { return raw_dscp_ecn & 0x03; }
        [[nodiscard]] auto total_length       () const -> std::uint16_t { return be16toh(raw_total_length); }
        [[nodiscard]] auto identification     () const -> std::uint16_t { return be16toh(raw_identification); }
        [[nodiscard]] auto flags              () const -> std::uint16_t { return be16toh(raw_flags_fragment_offset) >> 13; }
        [[nodiscard]] auto fragment_offset    () const -> std::uint16_t { return be16toh(raw_flags_fragment_offset) & 0x1fff; }
        [[nodiscard]] auto ttl                () const -> std::uint8_t  { return raw_ttl; }
        [[nodiscard]] auto protocol           () const -> std::uint8_t  { return raw_protocol; }
        [[nodiscard]] auto header_checksum    () const -> std::uint16_t { return be16toh(raw_header_checksum); }
        [[nodiscard]] auto source_address     () const -> std::uint32_t { return be32toh(raw_source_address); }
        [[nodiscard]] auto destination_address() const -> std::uint32_t { return be32toh(raw_destination_address); }

        [[nodiscard]] auto options_base()       { return reinterpret_cast<doca_be32_t      *>(this + 1); }
        [[nodiscard]] auto options_base() const { return reinterpret_cast<doca_be32_t const*>(this + 1); }
        [[nodiscard]] auto options_count() const -> std::size_t { return ihl() - 5; }

        [[nodiscard]] auto options()       -> std::span<doca_be32_t>       { return { options_base(), options_count() }; }
        [[nodiscard]] auto options() const -> std::span<doca_be32_t const> { return { options_base(), options_count() }; }

        [[nodiscard]] auto payload_base()       { return reinterpret_cast<std::byte*      >(options_base() + options_count()); }
        [[nodiscard]] auto payload_base() const { return reinterpret_cast<std::byte const*>(options_base() + options_count()); }

        [[nodiscard]] auto payload_length() const -> std::size_t { return total_length() - ihl() * 4; }

        [[nodiscard]] auto payload_bytes()       -> std::span<std::byte      > { return { payload_base(), payload_length() }; }
        [[nodiscard]] auto payload_bytes() const -> std::span<std::byte const> { return { payload_base(), payload_length() }; }

        [[nodiscard]] auto *udp_payload()       { return reinterpret_cast<udp_segment      *>(payload_base()); }
        [[nodiscard]] auto *udp_payload() const { return reinterpret_cast<udp_segment const*>(payload_base()); }

        auto &version(std::uint8_t value) {
            raw_version_ihl = value << 4 | ihl();
            return *this;
        }

        auto &ihl (std::uint8_t value) {
            raw_version_ihl = (raw_version_ihl & 0xf0) | value;
            return *this;
        }

        auto &dscp (std::uint8_t value) {
            raw_dscp_ecn = value << 2 | ecn();
            return *this;
        }

        auto &ecn (std::uint8_t value) {
            raw_dscp_ecn = (raw_dscp_ecn & 0xfc) | value;
            return *this;
        }

        auto &total_length(std::uint16_t value) {
            raw_total_length = htobe16(value);
            return *this;
        }

        auto &identification(std::uint16_t value) {
            raw_identification = htobe16(value);
            return *this;
        }

        auto &flags(std::uint16_t value) {
            raw_flags_fragment_offset = htobe16(value << 13 | fragment_offset());
            return *this;
        }

        auto &fragment_offset(std::uint16_t value) {
            raw_flags_fragment_offset = htobe16(flags() << 13 | value);
            return *this;
        }

        auto &ttl(std::uint8_t value) {
            raw_ttl = value;
            return *this;
        }

        auto &protocol(std::uint8_t value) {
            raw_protocol = value;
            return *this;
        }

        auto &header_checksum(std::uint16_t value) {
            raw_header_checksum = htobe16(value);
            return *this;
        }

        auto &source_address(std::uint32_t value) {
            raw_source_address = htobe32(value);
            return *this;
        }

        auto &destination_address(std::uint32_t value) {
            raw_destination_address = htobe32(value);
            return *this;
        }

    private:
        std::uint8_t raw_version_ihl;
        std::uint8_t raw_dscp_ecn;
        doca_be16_t raw_total_length;

        doca_be16_t raw_identification;
        doca_be16_t raw_flags_fragment_offset;

        std::uint8_t raw_ttl;
        std::uint8_t raw_protocol;
        doca_be16_t raw_header_checksum;

        doca_be32_t raw_source_address;
        doca_be32_t raw_destination_address;
    } __attribute__((packed));

    class ipv6_packet {
    public:
        [[nodiscard]] auto version            () const -> std::uint8_t                   { return static_cast<std::uint8_t>(be32toh(raw_version_traffic_class_flow_label) >> 28); }
        [[nodiscard]] auto traffic_class      () const -> std::uint8_t                   { return static_cast<std::uint8_t>(be32toh(raw_version_traffic_class_flow_label) >> 20 & 0xff);}
        [[nodiscard]] auto flow_label         () const -> std::uint32_t                  { return be32toh(raw_version_traffic_class_flow_label) & 0x000fffff; }
        [[nodiscard]] auto payload_length     () const -> std::uint16_t                  { return be16toh(raw_payload_length); }
        [[nodiscard]] auto next_header        () const -> std::uint8_t                   { return raw_next_header; }
        [[nodiscard]] auto hop_limit          () const -> std::uint8_t                   { return raw_hop_limit; }

        [[nodiscard]] auto source_address     ()       -> std::span<std::byte, 16>       { return raw_source_address; }
        [[nodiscard]] auto source_address     () const -> std::span<std::byte const, 16> { return raw_source_address; }
        [[nodiscard]] auto destination_address()       -> std::span<std::byte, 16>       { return raw_destination_address; }
        [[nodiscard]] auto destination_address() const -> std::span<std::byte const, 16> { return raw_destination_address; }

        auto &version(std::uint8_t value) { 
            auto vtcfl = be32toh(raw_version_traffic_class_flow_label);
            auto unchanged_bits = vtcfl & 0x0fffffff;
            auto shifted_value = static_cast<std::uint32_t>(value) << 28;

            raw_version_traffic_class_flow_label = htobe32(unchanged_bits | shifted_value);
            return *this;
        }

        auto &traffic_class(std::uint8_t  value) {
            auto vtcfl = be32toh(raw_version_traffic_class_flow_label);
            auto unchanged_bits = vtcfl & 0xf00fffff;
            auto shifted_value = static_cast<std::uint32_t>(value) << 20;

            raw_version_traffic_class_flow_label = htobe32(unchanged_bits | shifted_value);
            return *this;
        }

        auto &flow_label(std::uint32_t value) {
            auto vtcfl = be32toh(raw_version_traffic_class_flow_label);
            auto unchanged_bits = vtcfl & 0xfff00000;

            raw_version_traffic_class_flow_label = htobe32(unchanged_bits | value);
            return *this;
        }

        auto &payload_length(std::uint16_t value) {
            raw_payload_length = htobe16(value);
            return *this;
        }

        auto &next_header(std::uint8_t value) {
            raw_next_header = value;
            return *this;
        }
        auto &hop_limit(std::uint8_t value) {
            raw_hop_limit = value;
            return *this;
        }

    private:
        doca_be32_t raw_version_traffic_class_flow_label;
        doca_be16_t raw_payload_length;
        std::uint8_t raw_next_header;
        std::uint8_t raw_hop_limit;

        std::byte raw_source_address[16];
        std::byte raw_destination_address[16];

        std::byte payload[];
    } __attribute__((packed));

    class eth_frame {
    public:
        auto ethertype      () const -> std::uint16_t                 { return be16toh(raw_ethertype); }
        auto destination_mac()       -> std::span<std::byte      , 6> { return raw_destination_mac; }
        auto destination_mac() const -> std::span<std::byte const, 6> { return raw_destination_mac; }
        auto source_mac     ()       -> std::span<std::byte      , 6> { return raw_source_mac; }
        auto source_mac     () const -> std::span<std::byte const, 6> { return raw_source_mac; }

        auto ethertype(std::uint16_t value) { raw_ethertype = htobe16(value); }

        auto *ipv4_payload() { return &ipv4; }
        auto *ipv6_payload() { return &ipv6; }
    
    private:
        std::byte raw_destination_mac[6];
        std::byte raw_source_mac[6];
        doca_be16_t raw_ethertype;

        union {
            ipv4_packet ipv4;
            ipv6_packet ipv6;
        } __attribute__((packed));
    } __attribute__((packed));
}
