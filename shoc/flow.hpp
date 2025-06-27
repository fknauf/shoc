#pragma once

#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_flow.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <variant>
#include <vector>

/**
 * Wrappers for DOCA Flow functionality, see https://docs.nvidia.com/doca/sdk/doca+flow/index.html
 *
 * Unlike most other things in SHOC, little here is asynchronous or related to coroutines; rather it's
 * for the configuration and setup of flow pipes. Mostly what's added here is automatic resource management
 * a la C++'s RAII idiom and a fluent API for the configuration objects.
 *
 * The whole thing interfaces with eth_rxq as a forwarding target for RSS pipes.
 */
namespace shoc::flow {
    /**
     * Configuration for an RSS target, i.e. to load-balance packets between multiple eth_rxq. WIP,
     * currently not in use, API may change.
     *
     * TODO: Finish this.
     */
    class resource_rss_cfg {
    public:
        resource_rss_cfg(
            std::uint32_t outer_flags,
            std::uint32_t inner_flags,
            std::vector<std::uint16_t> queues,
            doca_flow_rss_hash_function rss_hash_func,
            doca_flow_resource_type resource_type = DOCA_FLOW_RESOURCE_TYPE_NONE // for flow_fwd
        ):
            queues_ { std::move(queues) },
            cfg_ {
                .outer_flags = outer_flags,
                .inner_flags = inner_flags,
                .queues_array = queues_.data(),
                .nr_queues = static_cast<int>(queues_.size()),
                .rss_hash_func = rss_hash_func
            },
            resource_type_ { resource_type }
        {}

        [[nodiscard]] auto const &doca_cfg() const {
            return cfg_;
        }

        [[nodiscard]] auto doca_cfg_ptr() const {
            return &cfg_;
        }

        [[nodiscard]] auto resource_type() const {
            return resource_type_;
        }

    private:
        std::vector<std::uint16_t> queues_;
        doca_flow_resource_rss_cfg cfg_;
        doca_flow_resource_type resource_type_;
    };

    class [[nodiscard]] library_scope;

    /**
     * Global configuration options for the whole DOCA FLow library.
     * See /opt/mellanox/doca/include/doca_flow.h for more information.
     * Not all functionality supported yet (callbacks in particular)
     *
     * This is intended as a builder for a library scope object and follows
     * a fluent API style, i.e. to be used as
     *
     * auto lib_scope = global_cfg{}.set_option1(...).set_option2(...).build();
     *
     * That'll initialize the library and uninitialize it when lib_scope goes
     * out of scope.
     */
    class global_cfg {
    public:
        [[nodiscard]] auto handle() const {
            return handle_.get();
        }

        [[nodiscard]] auto safe_handle() {
            ensure_handle_exists();
            return handle();
        }

        auto set_pipe_queues(
            std::uint16_t pipe_queues
        ) -> global_cfg &;
        
        auto set_nr_counters(
            std::uint32_t nr_counters
        ) -> global_cfg &;
        
        auto set_nr_meters(
            std::uint32_t nr_counters
        ) -> global_cfg &;
        
        auto set_nr_acl_collisions(
            std::uint8_t nr_acl_collisions
        ) -> global_cfg &;
        
        auto set_mode_args(
            char const *mode_args
        ) -> global_cfg &;
        
        auto set_nr_shared_resource(
            std::uint32_t nr_shared_resource,
            doca_flow_shared_resource_type type
        ) -> global_cfg &;
        
        auto set_queue_depth(
            std::uint32_t queue_depth
        ) -> global_cfg &;
        
        auto set_rss_key(
            std::span<std::byte const> rss_key
        ) -> global_cfg &;
        
        auto set_default_rss(
            resource_rss_cfg const &rss
        ) -> global_cfg &;

        //auto set_definitions(doca_flow_definitions const *defs) -> global_cfg &;

        //auto set_cb_pipe_process(doca_flow_pipe_process_cb cb) -> global_cfg &;
        //auto set_cb_entry_process(doca_flow_entry_process_cb cb) -> global_cfg &;
        //auto set_cb_shared_resource_unbind(doca_flow_shared_resource_unbind_cb) -> global_cfg &;

        [[nodiscard]] auto build() const -> library_scope;

    private:
        auto ensure_handle_exists() -> void;

        shared_handle<doca_flow_cfg, doca_flow_cfg_destroy> handle_;
    };

    /**
     * Manual init/deinit API: initialize the library from a global_cfg object.
     * Requires manual library teardown at the end of the program.
     */
    auto init(global_cfg const &cfg) -> doca_error_t;

    /**
     * Manual library teardown
     */
    auto destroy() -> void;

    /**
     * RAII anchor object for automatic library teardown, i.e. destroy() is called
     * when this goes out of scope. When global_cfg is used as a builder, this is
     * what's built.
     */
    struct library_scope {
        using config = global_cfg;

        library_scope(global_cfg const &cfg) {
            init(cfg);
        }

        ~library_scope() {
            destroy();
        }

        library_scope(library_scope const &) = delete;
        library_scope(library_scope &&) = delete;
        library_scope &operator=(library_scope const &) = delete;
        library_scope &operator=(library_scope &&) = delete;
    };

    class port;

    /**
     * Configuration for a DOCA Flow Port, which is where we get packets.
     * At least port and device should be set for this to function.
     * 
     * Follows a fluent api to implement a builder pattern, i.e.
     *
     * auto my_port = port_cfg{}.set_port_id(0).set_dev(dev).build();
     */
    class port_cfg {
    public:
        [[nodiscard]] auto handle() const { return handle_.get(); }
        [[nodiscard]] auto safe_handle() {
            ensure_handle_exists();
            return handle();
        }

        auto set_port_id(std::uint16_t) -> port_cfg &;
        auto set_devargs(char const *devargs) -> port_cfg&;
        auto set_priv_data_size(std::uint16_t priv_data_size) -> port_cfg&;
        auto set_dev(device dev) -> port_cfg&;
        auto set_rss_cfg(resource_rss_cfg const &rss) -> port_cfg &;
        auto set_ipsec_sn_offload_disable() -> port_cfg&;
        auto set_operation_state(doca_flow_port_operation_state state) -> port_cfg &;
        auto set_actions_mem_size(std::uint32_t size) -> port_cfg&;
        auto set_service_threads_core(std::uint32_t core) -> port_cfg&;
        auto set_service_threads_cycle(std::uint32_t cycle_ms) -> port_cfg&;

        auto port_id() const { return port_id_; }
        auto build() const -> port;

    private:
        auto ensure_handle_exists() -> void;

        unique_handle<doca_flow_port_cfg, doca_flow_port_cfg_destroy> handle_;
        std::uint16_t port_id_ = 65535;
    };

    /**
     * Flow port object to obtain or send out packets. Describes a hardware port or VF.
     *
     * Can be used as a source for packets in the ingress domain or a target in the egress
     * domain.
     */
    class port {
    public:
        using config = port_cfg;

        port() = default;
        port(port_cfg const &cfg);

        [[nodiscard]] auto handle() const { return handle_.get(); }

        auto pair(port &other) -> doca_error_t;

        [[nodiscard]] auto priv_data() -> std::uint8_t*;

        auto operation_state_modify(doca_flow_port_operation_state state) -> doca_error_t;
        auto calc_entropy(doca_flow_entropy_format &header) -> std::uint16_t;

        auto pipes_flush() -> void;
        auto pipes_dump(FILE *f) -> void;

        /**
         * After entries are slated to be added to pipes connected to this port, this function
         * must be called to batch-process them. Will only process entries submitted to the same
         * pipe queue; the idea there is that each CPU core should have a pipe queue of its own.
         *
         * @param pipe_queue ID of the pipe queue whose entries should be processed.
         * @param timeout timeout until failure is declared
         * @param max_processed_entries maximum number of entries to process
         */
        auto process_entries(
            std::uint16_t pipe_queue,
            std::chrono::microseconds timeout_us,
            std::uint32_t max_processed_entries
        ) -> doca_error_t;

        auto id() const noexcept { return port_id_; }

        auto shared_resources_bind(
            doca_flow_shared_resource_type type,
            std::span<std::uint32_t> resources
        ) -> doca_error_t;
    
    private:
        unique_handle<doca_flow_port, doca_flow_port_stop> handle_;
        std::uint16_t port_id_ = 65535;
    };

    // Experimental
    struct extended_actions {
    public:
        extended_actions(
            doca_flow_actions const &actions,
            std::optional<doca_flow_actions> const &mask = std::nullopt,
            std::vector<doca_flow_action_desc> descs = {}
        );

        [[nodiscard]] auto actions_ptr() { return &actions_; }
        [[nodiscard]] auto mask_ptr() { return mask_.has_value() ? &*mask_ : nullptr; }
        [[nodiscard]] auto descs_ptr() -> doca_flow_action_descs*;

        [[nodiscard]] auto &actions() { return actions_; }
        [[nodiscard]] auto &actions() const { return actions_; }
        [[nodiscard]] auto &mask() { return mask_; }
        [[nodiscard]] auto &mask() const { return mask_; }
        [[nodiscard]] auto &descs() { return descs_; }
        [[nodiscard]] auto &descs() const { return descs_; }

    private:
        doca_flow_actions actions_;
        std::optional<doca_flow_actions> mask_;
        std::vector<doca_flow_action_desc> descs_;
        doca_flow_action_descs descs_index_;
    };

    class pipe;

    struct fwd_none {};
    struct fwd_drop {};
    struct fwd_kernel {};

    /**
     * Variant type for possible Forwarding-Targets, largely for convenience. Backend library has
     * logic to convert its members to doca_flow_fwd.
     */
    using flow_fwd = std::variant<
        std::monostate,
        doca_flow_fwd,
        fwd_none,
        fwd_drop,
        fwd_kernel,
        std::reference_wrapper<resource_rss_cfg const>,
        std::reference_wrapper<pipe const>,
        std::reference_wrapper<port const>
    >;

    /**
     * Configuration object for a flow pipe, again as a fluent builder.
     */
    class pipe_cfg {
    public:
        pipe_cfg(port const &port);

        [[nodiscard]] auto handle() const {
            return handle_.get();
        }

        [[nodiscard]] auto build(
            flow_fwd fwd = std::monostate{},
            flow_fwd fwd_miss = std::monostate{}
        ) const -> pipe;

        auto set_match(
            doca_flow_match const &match,
            std::optional<doca_flow_match> const &match_mask = std::nullopt
        ) -> pipe_cfg &;

        auto set_actions(
            std::span<doca_flow_actions *const> actions,
            std::optional<std::span<doca_flow_actions *const>> actions_masks = std::nullopt,
            std::optional<std::span<doca_flow_action_descs *const>> action_descs = std::nullopt
        ) -> pipe_cfg &;

        auto set_actions(
            std::span<extended_actions> actions
        ) -> pipe_cfg &;

        auto set_actions(
            std::span<doca_flow_actions const> actions,
            std::optional<std::span<doca_flow_actions const>> actions_masks = std::nullopt,
            std::optional<std::span<doca_flow_action_desc const>> action_descs = std::nullopt
        ) -> pipe_cfg &;

        auto set_monitor(
            doca_flow_monitor const &monitor
        ) -> pipe_cfg &;

// unsupported in doca 2.x, slated to be re-enabled in 3.0.
// Some strange data type shenanigans going on here, will deal with that when it's clear what it'll look like.
//
//        auto set_ordered_lists(
//            std::span<doca_flow_ordered_list *const> ordered_lists
//        ) -> pipe_cfg &;

        auto set_name(
            char const *name
        ) -> pipe_cfg&;

        auto set_type(
            doca_flow_pipe_type type
        ) -> pipe_cfg&;

        auto set_domain(
            doca_flow_pipe_domain domain
        ) -> pipe_cfg&;

        auto set_is_root(
            bool is_root
        ) -> pipe_cfg&;

        auto set_nr_entries(
            std::uint32_t nr_entries
        ) -> pipe_cfg&;

        auto set_is_resizable(
            bool is_resizable
        ) -> pipe_cfg&;

        auto set_dir_info(
            doca_flow_direction_info dir_info
        ) -> pipe_cfg &;

        auto set_miss_counter(
            bool miss_counter
        ) -> pipe_cfg&;

        auto set_congestion_level_threshold(
            std::uint8_t congestion_level_threshold
        ) -> pipe_cfg&;

        auto set_user_ctx(
            void *user_ctx
        ) -> pipe_cfg&;

        auto set_hash_map_algorithm(
            std::uint32_t algorithm_flags
        ) -> pipe_cfg&;

    private:
        unique_handle<doca_flow_pipe_cfg, doca_flow_pipe_cfg_destroy> handle_;
    };

    /**
     * Handle to a pipe entry
     */
    class pipe_entry {
    public:
        pipe_entry() = default;
        pipe_entry(doca_flow_pipe_entry *handle):
            handle_ { handle }
        {}

        [[nodiscard]] auto handle() const { return handle_; }
        [[nodiscard]] auto status() const -> doca_flow_entry_status;
        [[nodiscard]] auto query() const -> doca_flow_resource_query;

    private:
        doca_flow_pipe_entry *handle_ = nullptr;
    };

    /**
     * Handle to a flow pipe.
     *
     * Right now, all pipe types are maanged through the same C++ type; this may change pending experience.
     *
     * User must take care to call the correct *_add_entry function for the pipe type, e.g. control_add_pipe
     * only makes sense if the pipe is a control pipe.
     */
    class pipe {
    public:
        using config = pipe_cfg;

        pipe(
            pipe_cfg const &cfg,
            flow_fwd fwd,
            flow_fwd fwd_miss
        );

        [[nodiscard]] auto handle() const { return handle_.get(); }

        auto shared_resources_bind(
            doca_flow_shared_resource_type type,
            std::span<std::uint32_t> resources
        ) -> doca_error_t;

        //auto resize(...);

        auto add_entry(
            std::uint16_t pipe_queue,
            doca_flow_match const &match,
            std::optional<doca_flow_actions> actions,
            std::optional<doca_flow_monitor> monitor,
            flow_fwd fwd,
            std::uint32_t flags,
            void *usr_ctx = nullptr
        ) -> pipe_entry;

        auto control_add_entry(
            std::uint16_t pipe_queue,
            std::uint32_t priority,
            doca_flow_match const &match,
            std::optional<doca_flow_match> const &match_mask,
            std::optional<doca_flow_match_condition> const &condition,
            std::optional<doca_flow_actions> const &actions,
            std::optional<doca_flow_actions> const &actions_mask,
            std::optional<doca_flow_action_descs> const &action_descs,
            std::optional<doca_flow_monitor> const &monitor,
            flow_fwd fwd,
            void *usr_ctx = nullptr
        ) -> pipe_entry;

        auto acl_add_entry(
            std::uint16_t pipe_queue,
            doca_flow_match const &match,
            std::optional<doca_flow_match> const &match_mask,
            std::uint32_t priority,
            flow_fwd fwd,
            doca_flow_flags_type flags,
            void *usr_ctx = nullptr
        ) -> pipe_entry;

        auto update_entry(
            std::uint16_t pipe_queue,
            doca_flow_match const &match,
            std::optional<doca_flow_actions> actions,
            std::optional<doca_flow_monitor> monitor,
            flow_fwd fwd,
            std::uint32_t flags
        ) -> pipe_entry;

        auto remove_entry(
            std::uint16_t pipe_queue,
            std::uint32_t flags,
            pipe_entry entry
        ) -> doca_error_t;

        auto query_pipe_miss() const -> doca_flow_resource_query;

    private:
        unique_handle<doca_flow_pipe, doca_flow_pipe_destroy> handle_;
    };
}
