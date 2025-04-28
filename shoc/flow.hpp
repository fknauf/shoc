#pragma once

#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_flow.h>

#include <cstdint>
#include <span>
#include <vector>

namespace shoc {
    class flow_resource_rss_cfg {
    public:
        flow_resource_rss_cfg(
            std::uint32_t outer_flags,
            std::uint32_t inner_flags,
            std::vector<std::uint16_t> queues,
            doca_flow_rss_hash_function rss_hash_func
        ):
            queues_ { std::move(queues) },
            cfg_ {
                .outer_flags = outer_flags,
                .inner_flags = inner_flags,
                .queues_array = queues_.data(),
                .nr_queues = static_cast<int>(queues_.size()),
                .rss_hash_func = rss_hash_func
            }
        {}

        [[nodiscard]] auto const &doca_cfg() const {
            return cfg_;
        }

        [[nodiscard]] auto doca_cfg_ptr() const {
            return &cfg_;
        }

    private:
        std::vector<std::uint16_t> queues_;
        doca_flow_resource_rss_cfg cfg_;
    };

    class flow_cfg {
    public:
        [[nodiscard]] auto handle() const {
            return handle_.get();
        }

        [[nodiscard]] auto safe_handle() {
            ensure_handle_exists();
            return handle();
        }

        auto set_pipe_queues(std::uint16_t pipe_queues) -> flow_cfg &;
        auto set_nr_counters(std::uint32_t nr_counters) -> flow_cfg &;
        auto set_nr_meters(std::uint32_t nr_counters) -> flow_cfg &;
        auto set_nr_acl_collisions(std::uint8_t nr_acl_collisions) -> flow_cfg &;
        auto set_mode_args(char const *mode_args) -> flow_cfg &;
        auto set_nr_shared_resource(std::uint32_t nr_shared_resource, doca_flow_shared_resource_type type) -> flow_cfg &;
        auto set_queue_depth(std::uint32_t queue_depth) -> flow_cfg &;

        auto set_rss_key(std::span<std::byte const> rss_key) -> flow_cfg &;
        auto set_default_rss(flow_resource_rss_cfg const &rss) -> flow_cfg &;

        //auto set_definitions(doca_flow_definitions const *defs) -> flow_cfg &;

        //auto set_cb_pipe_process(doca_flow_pipe_process_cb cb) -> flow_cfg &;
        //auto set_cb_entry_process(doca_flow_entry_process_cb cb) -> flow_cfg &;
        //auto set_cb_shared_resource_unbind(doca_flow_shared_resource_unbind_cb) -> flow_cfg &;

    private:
        auto ensure_handle_exists() -> void;

        unique_handle<doca_flow_cfg, doca_flow_cfg_destroy> handle_;
    };

    auto flow_init(flow_cfg const &cfg) -> doca_error_t;
    auto flow_destroy() -> void;

    struct flow_library_scope {
        flow_library_scope(flow_cfg const &cfg) {
            flow_init(cfg);
        }

        ~flow_library_scope() {
            flow_destroy();
        }

        flow_library_scope(flow_library_scope const &) = delete;
        flow_library_scope(flow_library_scope &&) = delete;
        flow_library_scope &operator=(flow_library_scope const &) = delete;
        flow_library_scope &operator=(flow_library_scope &&) = delete;
    };

    class flow_port;

    class flow_port_cfg {
    public:
        [[nodiscard]] auto handle() const { return handle_.get(); }
        [[nodiscard]] auto safe_handle() {
            ensure_handle_exists();
            return handle();
        }

        auto set_devargs(char const *devargs) -> flow_port_cfg&;
        auto set_priv_data_size(std::uint16_t priv_data_size) -> flow_port_cfg&;
        auto set_dev(device dev) -> flow_port_cfg&;
        auto set_rss_cfg(flow_resource_rss_cfg const &rss) -> flow_port_cfg &;
        auto set_ipsec_sn_offload_disable() -> flow_port_cfg&;
        auto set_operation_state(doca_flow_port_operation_state state) -> flow_port_cfg &;
        auto set_actions_mem_size(std::uint32_t size) -> flow_port_cfg&;
        auto set_service_threads_core(std::uint32_t core) -> flow_port_cfg&;
        auto set_service_threads_cycle(std::uint32_t cycle_ms) -> flow_port_cfg&;

        auto build() const -> flow_port;

    private:
        auto ensure_handle_exists() -> void;

        unique_handle<doca_flow_port_cfg, doca_flow_port_cfg_destroy> handle_;
    };

    class flow_port {
    public:
        flow_port() = default;
        flow_port(flow_port_cfg const &cfg);

        [[nodiscard]] auto handle() const { return handle_.get(); }

        auto pair(flow_port &other) -> doca_error_t;

        [[nodiscard]] auto priv_data() -> std::uint8_t*;

        auto operation_state_modify(doca_flow_port_operation_state state) -> doca_error_t;
        auto calc_entropy(doca_flow_entropy_format &header) -> std::uint16_t;

        auto pipes_flush() -> void;
        auto pipes_dump(FILE *f) -> void;
    
    private:
        unique_handle<doca_flow_port, doca_flow_port_stop> handle_;
    };
}
