#include "eth_frame.hpp"

namespace shoc {
    auto udp_segment::calculate_checksum(doca_be16_t pseudoheader_part) const -> std::uint16_t {
        doca_be32_t word_sum = pseudoheader_part;

        auto base_ptr = &raw_source_port;
        auto range_len = static_cast<std::size_t>(length()) + 1 / 2;
        auto words = std::span<doca_be16_t const> { base_ptr, range_len };

        for(auto w : words) {
            word_sum += w;
        }

        word_sum -= raw_checksum;
        word_sum = (word_sum & 0xffff) + (word_sum >> 16);
        word_sum = (word_sum & 0xffff) + (word_sum >> 16);

        return be16toh(~word_sum & 0xffff);
    }

    auto udp_segment::calculate_checksum(ipv4_packet const &wrapper) const -> std::uint16_t {
        auto source_ip = wrapper.source_address();
        auto dest_ip = wrapper.destination_address();

        std::uint32_t pseudoheader_sum = 17
            + (source_ip >> 16) + (source_ip & 0xffff)
            + (dest_ip >> 16) + (dest_ip & 0xffff)
            + length();

        doca_be16_t pseudoheader_part = htobe16((pseudoheader_sum & 0xffff) + (pseudoheader_sum >> 16));

        return calculate_checksum(pseudoheader_part);
    }

    auto udp_segment::calculate_checksum(ipv6_packet const &wrapper) const -> std::uint16_t {
        auto ips_base_ptr = reinterpret_cast<doca_be16_t const*>(wrapper.source_address().data());
        auto ips_range = std::span<doca_be16_t const> { ips_base_ptr, 16 };

        std::uint32_t pseudoheader_sum = htobe16(17) + raw_length;

        for(auto w : ips_range) {
            pseudoheader_sum += be16toh(w);
        }

        doca_be16_t pseudoheader_part =(pseudoheader_sum & 0xffff) + (pseudoheader_sum >> 16);

        return calculate_checksum(pseudoheader_part);
    }

    auto udp_segment::update_checksum(ipv4_packet const &wrapper) -> udp_segment* {
        raw_checksum = htobe16(calculate_checksum(wrapper));
        return this;
    }

    auto udp_segment::update_checksum(ipv6_packet const &wrapper) -> udp_segment* {
        raw_checksum = htobe16(calculate_checksum(wrapper));
        return this;
    }
}
