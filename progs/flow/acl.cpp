#include <shoc/flow.hpp>

#include <endian.h>

#include <chrono>
#include <thread>

inline auto be_ipv4_addr(
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d
) -> std::uint32_t {
    return htobe32((a << 24) | (b << 16) | (c << 8) | d);
}

auto create_port(std::uint16_t port_id) {
    return shoc::flow::port_cfg{}
        //.set_dev(...)
        .set_port_id(port_id)
        .set_operation_state(DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE)
        .set_actions_mem_size(4096)
        .build();
};

auto create_acl_pipe(
    shoc::flow::port &src
) {
    doca_flow_match match = {};
    match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = 0xffffffff;
    match.outer.ip4.dst_ip = 0xffffffff;
    match.outer.tcp.l4_port.src_port = 0xffff;
    match.outer.tcp.l4_port.dst_port = 0xffff;

    doca_flow_actions actions = {};
    doca_flow_actions *actions_idx[1] = { &actions };

    return shoc::flow::pipe::config{ src }
        .set_name("ACL_PIPE")
        .set_type(DOCA_FLOW_PIPE_ACL)
        .set_is_root(true)
        .set_nr_entries(10)
        .set_domain(DOCA_FLOW_PIPE_DOMAIN_DEFAULT)
        .set_match(match)
        .set_actions(actions_idx)
        .build(
            (doca_flow_fwd) {
                .type = DOCA_FLOW_FWD_DROP,
                .port_id = 0
            },
            std::monostate{}
        );
}

auto add_acl_specific_entry(
    shoc::flow::pipe &pipe,
    doca_be32_t src_ip_addr,
    doca_be32_t dst_ip_addr,
    doca_be16_t src_port,
    doca_be16_t dst_port,
    doca_flow_l4_type_ext l4_type,
    doca_be32_t src_ip_addr_mask,
    doca_be32_t dst_ip_addr_mask,
    doca_be16_t src_port_mask,
    doca_be16_t dst_port_mask,
    std::uint16_t priority,
    std::optional<std::uint16_t> fwd_port_id, // nullopt for deny
    doca_flow_flags_type flag
) {
    doca_flow_match match_mask = {};
    match_mask.parser_meta.outer_l4_type = static_cast<doca_flow_l4_meta>(0xffffffff);
    match_mask.outer.ip4.src_ip = src_ip_addr_mask;
    match_mask.outer.ip4.dst_ip = dst_ip_addr_mask;

    doca_flow_match match = {};
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = src_ip_addr;
    match.outer.ip4.dst_ip = dst_ip_addr;
    match.outer.l4_type_ext = l4_type;

    if(l4_type == DOCA_FLOW_L4_TYPE_EXT_TCP) {
        match_mask.outer.tcp.l4_port.src_port = src_port_mask;
        match_mask.outer.tcp.l4_port.dst_port = dst_port_mask;

        match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
        match.outer.tcp.l4_port.src_port = src_port;
        match.outer.tcp.l4_port.dst_port = dst_port;
    } else {
        match_mask.outer.udp.l4_port.src_port = src_port_mask;
        match_mask.outer.udp.l4_port.dst_port = dst_port_mask;

        match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
        match.outer.udp.l4_port.src_port = src_port;
        match.outer.udp.l4_port.dst_port = dst_port;
    }

    doca_flow_fwd fwd = {};

    if(fwd_port_id.has_value()) {
        fwd.type = DOCA_FLOW_FWD_PORT;
        fwd.port_id = *fwd_port_id;
    } else {
        fwd.type = DOCA_FLOW_FWD_DROP;
    }

    return pipe.acl_add_entry(0, match, match_mask, priority, fwd, flag, nullptr);
}

auto add_acl_pipe_entries(
    shoc::flow::pipe &pipe,
    std::uint16_t fwd_port_id
) {
    add_acl_specific_entry(
        pipe,
        be_ipv4_addr(1, 2, 3, 4),
        be_ipv4_addr(8, 8, 8, 8),
        htobe16(1234),
        htobe16(80),
        DOCA_FLOW_L4_TYPE_EXT_TCP,
        0xffffffff,
        0xffffffff,
        htobe16(0),
        htobe16(0),
        10,
        std::nullopt,
        DOCA_FLOW_WAIT_FOR_BATCH
    );

    add_acl_specific_entry(
        pipe,
        be_ipv4_addr(172, 20, 1, 4),
        be_ipv4_addr(192, 168, 3, 4),
        htobe16(1234),
        htobe16(80),
        DOCA_FLOW_L4_TYPE_EXT_UDP,
        0xffffffff,
        0xffffffff,
        htobe16(0),
        htobe16(3000),
        50,
        fwd_port_id,
        DOCA_FLOW_WAIT_FOR_BATCH
    );

    add_acl_specific_entry(
        pipe,
        be_ipv4_addr(172, 20, 1, 4),
        be_ipv4_addr(192, 168, 3, 4),
        htobe16(1234),
        htobe16(80),
        DOCA_FLOW_L4_TYPE_EXT_TCP,
        0xffffffff,
        0xffffffff,
        htobe16(1234),
        htobe16(0),
        40,
        fwd_port_id,
        DOCA_FLOW_WAIT_FOR_BATCH
    );

    add_acl_specific_entry(
        pipe,
        be_ipv4_addr(1, 2, 3, 5),
        be_ipv4_addr(8, 8, 8, 6),
        htobe16(1234),
        htobe16(80),
        DOCA_FLOW_L4_TYPE_EXT_TCP,
        htobe32(0xffffff00),
        htobe32(0xffffff00),
        htobe16(0xffff),
        htobe16(80),
        20,
        fwd_port_id,
        DOCA_FLOW_NO_WAIT
    );
}

auto main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> int {
    auto flow_lib = shoc::flow::library_scope::config{}
        .set_default_rss({
            0, 0, { 0, 1, 2, 3 }, DOCA_FLOW_RSS_HASH_FUNCTION_SYMMETRIC_TOEPLITZ
        })
        .set_pipe_queues(4)
        .set_mode_args("vnf,hws")
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

    auto port0 = create_port(0);
    auto port1 = create_port(1);

    auto acl_pipe0 = create_acl_pipe(port0);
    auto acl_pipe1 = create_acl_pipe(port1);

    add_acl_pipe_entries(acl_pipe0, 1);
    add_acl_pipe_entries(acl_pipe1, 0);

    using namespace std::chrono_literals;    

    port0.process_entries(0, 10ms, 4);
    port1.process_entries(0, 10ms, 4);

    std::this_thread::sleep_for(50s);
}
