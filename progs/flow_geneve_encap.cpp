#include <shoc/flow.hpp>

#include <cstddef>
#include <limits>

auto main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char *argv[]
) -> int {
    auto flow_lib = shoc::flow_cfg{}
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
        return shoc::flow_port_cfg{}
            //.set_dev(...)
            .set_port_id(port_id)
            .set_operation_state(DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE)
            .set_actions_mem_size(4096)
            .build();
    };

    auto port0 = create_port(0);
    auto port1 = create_port(1);

    port0.pair(port1);

    auto create_match_pipe = [&](shoc::flow_port const &src, std::uint16_t fwd_port_id) {
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

        return shoc::flow_pipe_cfg{ src }
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
    };

    auto match_pipe0 = create_match_pipe(port0, 1);
    auto match_pipe1 = create_match_pipe(port1, 0);
}
