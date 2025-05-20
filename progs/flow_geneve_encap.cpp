#include <shoc/flow.hpp>

#include <endian.h>

#include <cstddef>
#include <limits>

inline auto be_ipv4_addr(
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d
) -> std::uint32_t {
    return htobe32((a << 24) | (b << 16) | (c << 8) | d);
}

auto create_match_pipe(shoc::flow::port const &src, std::uint16_t fwd_port_id) {
    doca_flow_match match = {};
    match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
    match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = 0xffffffff;
    match.outer.ip4.dst_ip = 0xffffffff;
    match.outer.tcp.l4_port.src_port = 0xffff;
    match.outer.tcp.l4_port.dst_port = 0xffff;

    doca_flow_actions actions;
    actions.meta.pkt_meta = std::numeric_limits<std::uint32_t>::max();
    doca_flow_actions *actions_index[] = { &actions };

    return shoc::flow::pipe_cfg{ src }
        .set_name("MATCH_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_match(match)
        .set_actions(actions_index)
        .build(
            (doca_flow_fwd) {
                .type = DOCA_FLOW_FWD_PORT,
                .port_id = fwd_port_id
            },
            std::monostate{}
        );
}

auto add_match_pipe_entries(shoc::flow::pipe &pipe) {
    doca_flow_match match = {};
    match.outer.ip4.dst_ip = be_ipv4_addr(8, 8, 8, 8);
    match.outer.ip4.src_ip = be_ipv4_addr(1, 2, 3, 4);
    match.outer.tcp.l4_port.dst_port = htobe16(80);
    match.outer.tcp.l4_port.src_port = htobe16(1234);

    doca_flow_actions actions = {};
    actions.meta.pkt_meta = htobe32(1);
    actions.action_idx = 0;
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    match.outer.tcp.l4_port.src_port = htobe16(2345);
    actions.meta.pkt_meta = htobe32(2);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    match.outer.tcp.l4_port.src_port = htobe16(3456);
    actions.meta.pkt_meta = htobe32(3);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    match.outer.tcp.l4_port.src_port = htobe16(4567);
    actions.meta.pkt_meta = htobe32(4);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);
};

auto create_geneve_encap_pipe(shoc::flow::port &port, std::uint16_t fwd_port_id) {
    doca_flow_match match = {};
    doca_flow_match match_mask = {};
    match_mask.meta.pkt_meta = std::numeric_limits<std::uint32_t>::max();

    doca_flow_actions actions[4] = { {}, {}, {}, {} };

    for(auto &a : actions) {
        a.encap_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
        std::ranges::fill(a.encap_cfg.encap.outer.eth.src_mac, 0xff);
        std::ranges::fill(a.encap_cfg.encap.outer.eth.dst_mac, 0xff);
        a.encap_cfg.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
        a.encap_cfg.encap.outer.ip4.src_ip = 0xffffffff;
        a.encap_cfg.encap.outer.ip4.dst_ip = 0xffffffff;
        a.encap_cfg.encap.outer.ip4.ttl = 0xff;
        a.encap_cfg.encap.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
        a.encap_cfg.encap.outer.udp.l4_port.dst_port = htobe16(DOCA_FLOW_GENEVE_DEFAULT_PORT);
        a.encap_cfg.encap.tun.type = DOCA_FLOW_TUN_GENEVE;
        a.encap_cfg.encap.tun.geneve.vni = 0xffffffff;
    }
        
    actions[0].encap_cfg.is_l2 = false;
    actions[0].encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_IPV4);

    actions[1].encap_cfg.is_l2 = true;
    actions[1].encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_IPV4);
    actions[1].encap_cfg.encap.tun.geneve.ver_opt_len = 5;
    std::ranges::fill(
        std::span { actions[1].encap_cfg.encap.tun.geneve_options, 5},
        (doca_flow_geneve_option) { .data = 0xffffffff }
    );

    actions[2].encap_cfg.is_l2 = true;
    actions[2].encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_TEB);

    actions[3].encap_cfg.is_l2 = true;
    actions[3].encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_TEB);
    actions[3].encap_cfg.encap.tun.geneve.ver_opt_len = 5;
    std::ranges::fill(
        std::span { actions[3].encap_cfg.encap.tun.geneve_options, 5},
        (doca_flow_geneve_option) { .data = 0xffffffff }
    );

    doca_flow_actions *actions_idx[4] = {
        &actions[0],
        &actions[1],
        &actions[2],
        &actions[3]
    };

    return shoc::flow::pipe_cfg{ port }
        .set_name("GENEVE_ENCAP_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_domain(DOCA_FLOW_PIPE_DOMAIN_EGRESS)
        .set_match(match, match_mask)
        .set_actions(actions_idx)
        .build(
            (doca_flow_fwd) {
                .type = DOCA_FLOW_FWD_PORT,
                .port_id = fwd_port_id
            },
            std::monostate{}
        );
}

auto add_geneve_encap_entries(shoc::flow::pipe &pipe) {
    doca_be32_t encap_dst_ip_addr = be_ipv4_addr(81, 81, 81, 81);
    doca_be32_t encap_src_ip_addr = be_ipv4_addr(11, 21, 31, 41);
    std::uint8_t encap_ttl = 17;
    std::uint8_t src_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    std::uint8_t dst_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    doca_flow_match match = {};
    doca_flow_actions actions = {};

    std::ranges::copy(src_mac, actions.encap_cfg.encap.outer.eth.src_mac);
    std::ranges::copy(dst_mac, actions.encap_cfg.encap.outer.eth.dst_mac);
    actions.encap_cfg.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    actions.encap_cfg.encap.outer.ip4.src_ip = encap_src_ip_addr;
    actions.encap_cfg.encap.outer.ip4.dst_ip = encap_dst_ip_addr;
    actions.encap_cfg.encap.outer.ip4.ttl = encap_ttl;
    actions.encap_cfg.encap.tun.type = DOCA_FLOW_TUN_GENEVE;

    /* L3 encap - GENEVE header only */
    actions.encap_cfg.encap.tun.geneve.vni = 0xadadad << 8;
    actions.encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_IPV4);
    actions.action_idx = 0;
    match.meta.pkt_meta = htobe32(1);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    /* L3 encap - GENEVE header */
    actions.encap_cfg.encap.tun.geneve.vni = 0xcdcdcd << 8;
    actions.encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_IPV4);
    actions.encap_cfg.encap.tun.geneve.ver_opt_len = 5;
    /* First option */
    actions.encap_cfg.encap.tun.geneve_options[0].class_id = htobe16(0x0107);
    actions.encap_cfg.encap.tun.geneve_options[0].type = 1;
    actions.encap_cfg.encap.tun.geneve_options[0].length = 2;
    actions.encap_cfg.encap.tun.geneve_options[1].data = htobe32(0x01234567);
    actions.encap_cfg.encap.tun.geneve_options[2].data = htobe32(0x89abcdef);
    /* Second option */
    actions.encap_cfg.encap.tun.geneve_options[3].class_id = htobe16(0x0107);
    actions.encap_cfg.encap.tun.geneve_options[3].type = 2;
    actions.encap_cfg.encap.tun.geneve_options[3].length = 1;
    actions.encap_cfg.encap.tun.geneve_options[4].data = htobe32(0xabbadeba);
    actions.action_idx = 1;
    match.meta.pkt_meta = htobe32(2);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    /* L2 encap - GENEVE header only */
    actions.encap_cfg.encap.tun.geneve.vni = 0xefefef << 8;
    actions.encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_TEB);
    actions.encap_cfg.encap.tun.geneve.ver_opt_len = 0;
    actions.action_idx = 2;
    match.meta.pkt_meta = htobe32(3);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);

    /* L2 encap - GENEVE header */
    actions.encap_cfg.encap.tun.geneve.vni = 0x123456 << 8;
    actions.encap_cfg.encap.tun.geneve.next_proto = htobe16(DOCA_FLOW_ETHER_TYPE_TEB);
    actions.encap_cfg.encap.tun.geneve.ver_opt_len = 5;
    /* Option header */
    actions.encap_cfg.encap.tun.geneve_options[0].class_id = htobe16(0x0107);
    actions.encap_cfg.encap.tun.geneve_options[0].type = 3;
    actions.encap_cfg.encap.tun.geneve_options[0].length = 4;
    /* Option data */
    actions.encap_cfg.encap.tun.geneve_options[1].data = htobe32(0x11223344);
    actions.encap_cfg.encap.tun.geneve_options[2].data = htobe32(0x55667788);
    actions.encap_cfg.encap.tun.geneve_options[3].data = htobe32(0x99aabbcc);
    actions.encap_cfg.encap.tun.geneve_options[4].data = htobe32(0xddeeff00);
    actions.action_idx = 3;
    match.meta.pkt_meta = htobe32(4);
    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0, nullptr);
};

auto main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> int {
    auto flow_lib = shoc::flow::global_cfg{}
        .set_default_rss({
            0, 0, { 0, 1, 2, 3 }, DOCA_FLOW_RSS_HASH_FUNCTION_SYMMETRIC_TOEPLITZ
        })
        .set_pipe_queues(4)
        .set_mode_args("vnf, hw")
        .set_nr_counters(0)
        .set_nr_meters(0)
        //.set_cb_entry_process(...)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_METER)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_COUNTER)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_RSS)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_MIRROR)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_PSP)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_ENCAP)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_DECAP)
        .set_nr_shared_resource(0, DOCA_FLOW_SHARED_RESOURCE_IPSEC_SA)
        //.set_definitions(...)
        .build();

    auto create_port = [&](std::uint16_t port_id) {
        return shoc::flow::port_cfg{}
            //.set_dev(...)
            .set_port_id(port_id)
            .set_operation_state(DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE)
            .set_actions_mem_size(4096)
            .build();
    };

    auto port0 = create_port(0);
    auto port1 = create_port(1);

    port0.pair(port1);

    auto match_pipe0 = create_match_pipe(port0, 1);
    auto match_pipe1 = create_match_pipe(port1, 0);

    add_match_pipe_entries(match_pipe0);
    add_match_pipe_entries(match_pipe1);

    auto tun_pipe0 = create_geneve_encap_pipe(port1, 1);
    auto tun_pipe1 = create_geneve_encap_pipe(port0, 0);

    add_geneve_encap_entries(tun_pipe0);
    add_geneve_encap_entries(tun_pipe1);

    port0.process_entries(0, std::chrono::microseconds(10000), 4);
    port1.process_entries(0, std::chrono::microseconds(10000), 4);
}
