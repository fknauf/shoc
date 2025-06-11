#include <shoc/flow.hpp>
#include <shoc/logger.hpp>

#include <endian.h>

#include <chrono>
#include <numeric>
#include <ranges>
#include <thread>

inline auto be_ipv4_addr(
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d
) -> std::uint32_t {
    return htobe32((a << 24) | (b << 16) | (c << 8) | d);
}

struct aging_user_data {
    int nb_processed = 0;
    bool failure = false;
};

auto check_for_valid_entry_aging(
    doca_flow_pipe_entry *entry,
    std::uint16_t pipe_queue,
    doca_flow_entry_status status,
    doca_flow_entry_op op,
    void *user_ctx
) -> void {
    auto aging_data = static_cast<aging_user_data*>(user_ctx);

    if(status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
        aging_data->failure = true;
    }

    if(op == DOCA_FLOW_ENTRY_OP_AGED) {
        auto err = doca_flow_pipe_remove_entry(pipe_queue, DOCA_FLOW_NO_WAIT, entry);

        if(err != DOCA_SUCCESS) {
            shoc::logger->error("failed to remove entry: {}", doca_error_get_descr(err));
        }
    } else {
        ++aging_data->nb_processed;
    }
}

auto create_port(std::uint16_t port_id) {
    return shoc::flow::port_cfg{}
        //.set_dev(...)
        .set_port_id(port_id)
        .set_operation_state(DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE)
        .set_actions_mem_size(4096)
        .build();
};

auto create_aging_pipe(
    shoc::flow::port &port,
    shoc::flow::port &fwd_port,
    std::uint32_t n_entries
) {
    doca_flow_match match = {};
    match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
    match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
    match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
    match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
    match.outer.ip4.src_ip = 0xffffffff;
    match.outer.ip4.dst_ip = 0xffffffff;
    match.outer.tcp.l4_port.src_port = 0xffff;
    match.outer.tcp.l4_port.dst_port = 0xffff;

    doca_flow_monitor monitor = {};
    monitor.aging_sec = 0xffffffff;

    doca_flow_actions actions = {};
    doca_flow_actions *actions_idx[] = { &actions };

    return shoc::flow::pipe::config{ port }
        .set_name("AGING_PIPE")
        .set_type(DOCA_FLOW_PIPE_BASIC)
        .set_is_root(true)
        .set_match(match)
        .set_actions(actions_idx)
        .set_monitor(monitor)
        .set_miss_counter(true)
        .set_nr_entries(n_entries)
        .build(fwd_port, std::monostate{});
}

auto add_aging_pipe_entries(
    shoc::flow::pipe &pipe,
    shoc::flow::port &src_port,
    std::uint32_t num_of_aging_entries
) {
    for(auto i : std::views::iota(std::uint32_t{0}, num_of_aging_entries)) {
        doca_flow_monitor monitor = {};
        monitor.aging_sec = 5;

        doca_flow_match match = {};
        match.outer.ip4.dst_ip = be_ipv4_addr(8, 8, 8, 8);
        match.outer.ip4.src_ip = be_ipv4_addr(1, 2, 3, 4);
        match.outer.tcp.l4_port.dst_port = htobe16(80);
        match.outer.tcp.l4_port.src_port = htobe16(1234);

        doca_flow_actions actions = {};

        std::uint32_t flags = (i + 1 == num_of_aging_entries) ? DOCA_FLOW_NO_WAIT : 0;

        pipe.add_entry(0, match, actions, monitor, std::monostate{}, flags);
    }

    using namespace std::chrono_literals;    

    src_port.process_entries(0, 10ms, num_of_aging_entries);
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

    auto const num_of_aging_entries = 10;
    auto pipe0 = create_aging_pipe(port0, port1, num_of_aging_entries);
    auto pipe1 = create_aging_pipe(port1, port0, num_of_aging_entries);

    add_aging_pipe_entries(pipe0, port0, num_of_aging_entries);
    add_aging_pipe_entries(pipe1, port1, num_of_aging_entries);

    using namespace std::chrono_literals;    

    std::this_thread::sleep_for(5s);

    [[maybe_unused]] auto q1 = pipe0.query_pipe_miss();
    [[maybe_unused]] auto q2 = pipe1.query_pipe_miss();
}
