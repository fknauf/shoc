#include <shoc/flow.hpp>

#include <endian.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <limits>
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

auto create_match_meta_pipe(
    shoc::flow::port &src,
    shoc::flow::port &fwd_port
) {
    doca_flow_match match_mask = {};
    doca_flow_match match = {};
    doca_flow_actions actions = {};
    doca_flow_actions *actions_idx[] = { &actions };

    match_mask.meta.u32[0] = 0xffffffffu;

    return shoc::flow::pipe::config{ src }
        .set_name("MATCH_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(false)
        .set_match(match, match_mask)
        .set_actions(actions_idx)
        .build(fwd_port, std::monostate{});
}

auto add_match_meta_pipe_entry(
    shoc::flow::pipe &pipe
) {
    doca_flow_match match = {};
    match.meta.u32[0] = htobe32(0x02040608);
    doca_flow_actions actions = {};

    return pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0);
}

auto create_add_to_meta_pipe(
    shoc::flow::port &port,
    shoc::flow::pipe &match_meta_pipe
) {
    doca_flow_action_desc desc_array[2] = {
        {
            .type = DOCA_FLOW_ACTION_COPY,
            .field_op = {
                .src = {
                    .field_string = "outer.ipv4.src_ip",
                    .bit_offset = 0
                },
                .dst = {
                    .field_string = "meta.data",
                    .bit_offset = offsetof(doca_flow_meta, u32[0])
                },
                .width = 32
            }
        },
        {
            .type = DOCA_FLOW_ACTION_ADD,
            .field_op = {
                .src = {
                    .field_string = "outer.ipv4.src_ip",
                    .bit_offset = 0
                },
                .dst = {
                    .field_string = "meta.data",
                    .bit_offset = offsetof(doca_flow_meta, u32[0])
                },
                .width = 32
            }
        }
    };

    doca_flow_action_descs descs = {
        .nb_action_desc = 2,
        .desc_array = desc_array
    };    
    doca_flow_action_descs *descs_idx[] = { &descs };

    doca_flow_match match = {};
    match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = 0xffffffff;
    match.outer.ip4.dst_ip = 0xffffffff;
    match.outer.tcp.l4_port.src_port = 0xffff;
    match.outer.tcp.l4_port.dst_port = 0xffff;

    doca_flow_actions actions = {};
    doca_flow_actions *actions_idx[] = { &actions };

    return shoc::flow::pipe::config{ port }
        .set_name("ADD_TO_META_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_match(match)
        .set_actions(actions_idx, std::nullopt, descs_idx)
        .build(match_meta_pipe, std::monostate{});
}

auto add_add_to_meta_pipe_entry(
    shoc::flow::pipe &pipe
) {
    doca_flow_match match = {};
    match.outer.ip4.dst_ip = be_ipv4_addr(8, 8, 8, 8);
    match.outer.ip4.src_ip = be_ipv4_addr(1, 2, 3, 4);
    match.outer.tcp.l4_port.dst_port = htobe16(80);
    match.outer.tcp.l4_port.src_port = htobe16(1234);

    doca_flow_actions actions = {};

    pipe.add_entry(0, match, actions, std::nullopt, std::monostate{}, 0);
}

auto main() -> int try {
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

    auto match_pipe0 = create_match_meta_pipe(port0, port1);
    auto match_pipe1 = create_match_meta_pipe(port1, port0);

    add_match_meta_pipe_entry(match_pipe0);
    add_match_meta_pipe_entry(match_pipe1);

    auto add_pipe0 = create_add_to_meta_pipe(port0, match_pipe0);
    auto add_pipe1 = create_add_to_meta_pipe(port1, match_pipe1);

    using namespace std::chrono_literals;    

    port0.process_entries(0, 10ms, 2);
    port1.process_entries(0, 10ms, 2);

    std::this_thread::sleep_for(5s);
} catch(shoc::doca_exception &e) {
    std::cerr << e.what() << std::endl;
}
