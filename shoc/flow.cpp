#include "flow.hpp"

#include "common/overload.hpp"
#include "error.hpp"

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

namespace shoc {
    /////////////////////
    // flow_cfg
    /////////////////////

    auto flow_cfg::ensure_handle_exists() -> void {
        if(!handle_) {
            doca_flow_cfg *raw_handle;
            enforce_success(doca_flow_cfg_create(&raw_handle));
            handle_.reset(raw_handle);
        }
    }

    auto flow_cfg::set_pipe_queues(std::uint16_t pipe_queues) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_pipe_queues(safe_handle(), pipe_queues));
        return *this;
    }

    auto flow_cfg::set_nr_counters(std::uint32_t nr_counters) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_nr_counters(safe_handle(), nr_counters));
        return *this;
    }

    auto flow_cfg::set_nr_meters(std::uint32_t nr_meters) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_nr_meters(safe_handle(), nr_meters));
        return *this;
    }

    auto flow_cfg::set_nr_acl_collisions(std::uint8_t nr_acl_collisions) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_nr_acl_collisions(safe_handle(), nr_acl_collisions));
        return *this;
    }

    auto flow_cfg::set_mode_args(char const *mode_args) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_mode_args(safe_handle(), mode_args));
        return *this;
    }

    auto flow_cfg::set_nr_shared_resource(
        std::uint32_t nr_shared_resource,
        doca_flow_shared_resource_type type
    ) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_nr_shared_resource(safe_handle(), nr_shared_resource, type));
        return *this;
    }

    auto flow_cfg::set_queue_depth(std::uint32_t queue_depth) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_nr_counters(safe_handle(), queue_depth));
        return *this;
    }

    auto flow_cfg::set_rss_key(std::span<std::byte const> rss_key) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_rss_key(
            safe_handle(),
            reinterpret_cast<std::uint8_t const *>(rss_key.data()),
            rss_key.size()
        ));
        return *this;
    }

    auto flow_cfg::set_default_rss(flow_resource_rss_cfg const &rss) -> flow_cfg & {
        enforce_success(doca_flow_cfg_set_default_rss(safe_handle(), rss.doca_cfg_ptr()));
        return *this;
    }

    //auto flow_cfg::set_definitions(doca_flow_definitions const *defs) -> flow_cfg &;

    //auto flow_cfg::set_cb_pipe_process(doca_flow_pipe_process_cb cb) -> flow_cfg &;
    //auto flow_cfg::set_cb_entry_process(doca_flow_entry_process_cb cb) -> flow_cfg &;
    //auto flow_cfg::set_cb_shared_resource_unbind(doca_flow_shared_resource_unbind_cb) -> flow_cfg &;

    auto flow_cfg::build() const -> flow_library_scope {
        return { *this };
    }

    auto flow_init(flow_cfg const &cfg) -> doca_error_t {
        return doca_flow_init(cfg.handle());
    }

    auto flow_destroy() -> void {
        doca_flow_destroy();
    }

    /////////////////////
    // flow_port_cfg
    /////////////////////

    auto flow_port_cfg::ensure_handle_exists() -> void {
        if(!handle_) {
            doca_flow_port_cfg *raw_handle;
            enforce_success(doca_flow_port_cfg_create(&raw_handle));
            handle_.reset(raw_handle);
        }
    }

    auto flow_port_cfg::set_port_id(std::uint16_t port_id) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_port_id(safe_handle(), port_id));
        return *this;
    }

    auto flow_port_cfg::set_devargs(char const *devargs) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_devargs(safe_handle(), devargs));
        return *this;
    }

    auto flow_port_cfg::set_priv_data_size(std::uint16_t priv_data_size) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_priv_data_size(safe_handle(), priv_data_size));
        return *this;
    }

    auto flow_port_cfg::set_dev(device dev) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_dev(safe_handle(), dev.handle()));
        return *this;
    }

    auto flow_port_cfg::set_rss_cfg(flow_resource_rss_cfg const &rss) -> flow_port_cfg & {
        enforce_success(doca_flow_port_cfg_set_rss_cfg(safe_handle(), rss.doca_cfg_ptr()));
        return *this;
    }

    auto flow_port_cfg::set_ipsec_sn_offload_disable() -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_ipsec_sn_offload_disable(safe_handle()));
        return *this;
    }

    auto flow_port_cfg::set_operation_state(doca_flow_port_operation_state state) -> flow_port_cfg & {
        enforce_success(doca_flow_port_cfg_set_operation_state(safe_handle(), state));
        return *this;
    }

    auto flow_port_cfg::set_actions_mem_size(std::uint32_t size) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_actions_mem_size(safe_handle(), size));
        return *this;
    }

    auto flow_port_cfg::set_service_threads_core(std::uint32_t core) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_service_threads_core(safe_handle(), core));
        return *this;
    }

    auto flow_port_cfg::set_service_threads_cycle(std::uint32_t cycle_ms) -> flow_port_cfg& {
        enforce_success(doca_flow_port_cfg_set_service_threads_cycle(safe_handle(), cycle_ms));
        return *this;
    }

    auto flow_port_cfg::build() const -> flow_port {
        return { *this };
    }

    flow_port::flow_port(flow_port_cfg const &cfg) {
        doca_flow_port *raw_handle;
        enforce_success(doca_flow_port_start(cfg.handle(), &raw_handle));
        handle_.reset(raw_handle);
    }

    auto flow_port::pair(flow_port &other) -> doca_error_t {
        return doca_flow_port_pair(handle(), other.handle());
    }

    auto flow_port::operation_state_modify(doca_flow_port_operation_state state) -> doca_error_t {
        return doca_flow_port_operation_state_modify(handle(), state);
    }

    auto flow_port::calc_entropy(doca_flow_entropy_format &header) -> std::uint16_t {
        std::uint16_t entropy;

        enforce_success(doca_flow_port_calc_entropy(handle(), &header, &entropy));
        return entropy;
    }

    auto flow_port::pipes_flush() -> void {
        doca_flow_port_pipes_flush(handle());
    }

    auto flow_port::pipes_dump(FILE *dest) -> void {
        doca_flow_port_pipes_dump(handle(), dest);
    }

    /////////////////////////
    // flow_extended_actions
    /////////////////////////

    flow_extended_actions::flow_extended_actions(
        doca_flow_actions const &actions,
        std::optional<doca_flow_actions> const &mask,
        std::vector<doca_flow_action_desc> descs
    ):
        actions_ { actions },
        mask_ { mask },
        descs_ { std::move(descs) }
    {
        enforce(descs.size() < 256, DOCA_ERROR_INVALID_VALUE);
    }

    auto flow_extended_actions::descs_ptr() -> doca_flow_action_descs* {
        descs_index_ = { 
            .nb_action_desc = static_cast<std::uint8_t>(descs_.size()),
            .desc_array = descs_.data()
        };

        return &descs_index_;
    }

    /////////////////////////
    // flow_pipe_cfg
    /////////////////////////


    flow_pipe_cfg::flow_pipe_cfg(flow_port const &port) {
        doca_flow_pipe_cfg *raw_handle;
        enforce_success(doca_flow_pipe_cfg_create(&raw_handle, port.handle()));
        handle_.reset(raw_handle);
    }

    auto flow_pipe_cfg::build(
        flow_fwd fwd,
        flow_fwd fwd_miss
    ) const -> flow_pipe {
        return { *this, std::move(fwd), std::move(fwd_miss) };
    }

    auto flow_pipe_cfg::set_match(
        doca_flow_match const &match,
        std::optional<doca_flow_match> const &match_mask
    ) -> flow_pipe_cfg & {
        enforce_success(doca_flow_pipe_cfg_set_match(
            handle(),
            &match,
            match_mask.has_value() ? &*match_mask : nullptr
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_actions(
        std::span<doca_flow_actions *const> actions,
        std::optional<std::span<doca_flow_actions *const>> actions_masks,
        std::optional<std::span<doca_flow_action_descs *const>> action_descs
    ) -> flow_pipe_cfg & {
        enforce(!actions_masks.has_value() || actions_masks->size() == actions.size(), DOCA_ERROR_INVALID_VALUE);
        enforce(!action_descs.has_value() || action_descs->size() == actions.size(), DOCA_ERROR_INVALID_VALUE);

        enforce_success(doca_flow_pipe_cfg_set_actions(
            handle(),
            actions.data(),
            actions_masks.has_value() ? actions_masks->data() : nullptr,
            action_descs.has_value() ? action_descs->data() : nullptr,
            actions.size()
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_actions(
        std::span<flow_extended_actions> actions
    ) -> flow_pipe_cfg & {
        auto actions_index = boost::container::small_vector<doca_flow_actions*, 128>(actions.size());
        auto masks_index = boost::container::small_vector<doca_flow_actions*, 128>(actions.size());
        auto descs_index = boost::container::small_vector<doca_flow_action_descs*, 128>(actions.size());

        for(auto i : std::views::iota(std::size_t{0}, actions.size())) {
            actions_index[i] = actions[i].actions_ptr();
            masks_index[i] = actions[i].mask_ptr();
            descs_index[i] = actions[i].descs_ptr();
        }

        return set_actions(actions_index, masks_index, descs_index);
    }

    auto flow_pipe_cfg::set_monitor(
        doca_flow_monitor const &monitor
    ) -> flow_pipe_cfg & {
        enforce_success(doca_flow_pipe_cfg_set_monitor(
            handle(),
            &monitor
        ));
        return *this;
    }

//    auto flow_pipe_cfg::set_ordered_lists(
//        std::span<doca_flow_ordered_list *const> ordered_lists
//    ) -> flow_pipe_cfg & {
//        enforce_success(doca_flow_pipe_cfg_set_ordered_lists(
//            handle(),
//            ordered_lists.data(),
//            ordered_lists.size()
//        ));
//        return *this;
//    }

    auto flow_pipe_cfg::set_name(
        char const *name
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_name(
            handle(),
            name
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_type(
        doca_flow_pipe_type type
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_type(
            handle(),
            type
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_domain(
        doca_flow_pipe_domain domain
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_domain(
            handle(),
            domain
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_is_root(
        bool is_root
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_is_root(
            handle(),
            is_root
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_nr_entries(
        std::uint32_t nr_entries
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_nr_entries(
            handle(),
            nr_entries
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_is_resizable(
        bool is_resizable
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_is_resizable(
            handle(),
            is_resizable
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_dir_info(
        doca_flow_direction_info dir_info
    ) -> flow_pipe_cfg & {
        enforce_success(doca_flow_pipe_cfg_set_dir_info(
            handle(),
            dir_info
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_miss_counter(
        bool miss_counter
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_miss_counter(
            handle(),
            miss_counter
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_congestion_level_threshold(
        std::uint8_t congestion_level_threshold
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_congestion_level_threshold(
            handle(),
            congestion_level_threshold
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_user_ctx(
        void *user_ctx
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_user_ctx(
            handle(),
            user_ctx
        ));
        return *this;
    }

    auto flow_pipe_cfg::set_hash_map_algorithm(
        std::uint32_t algorithm_flags
    ) -> flow_pipe_cfg& {
        enforce_success(doca_flow_pipe_cfg_set_hash_map_algorithm(
            handle(),
            algorithm_flags
        ));
        return *this;
    }

    /////////////////////
    // flow_pipe
    /////////////////////

    namespace {
        auto docaify_fwd(flow_fwd const &f) {
            return std::visit(
                overload {
                    [](std::monostate) -> doca_flow_fwd {
                       return {
                            .type = DOCA_FLOW_FWD_NONE,
                            .target = nullptr
                        };
                    },
                    [](doca_flow_fwd dff) -> doca_flow_fwd {
                        return dff;
                    },
                    [](flow_resource_rss_cfg const &rss_cfg) -> doca_flow_fwd {
                        return {
                            .type = DOCA_FLOW_FWD_RSS,
                            .rss_type = rss_cfg.resource_type(),
                            .rss = rss_cfg.doca_cfg()
                        };
                    },
                    [](flow_pipe const &pipe) -> doca_flow_fwd {
                        return {
                            .type = DOCA_FLOW_FWD_PIPE,
                            .next_pipe = pipe.handle()
                        };
                    },
                },
                f
            );
        }
    }

    flow_pipe::flow_pipe(
        flow_pipe_cfg const &cfg,
        flow_fwd fwd,
        flow_fwd fwd_miss
    ) {
        auto fwd_doca = docaify_fwd(fwd);
        auto fwd_miss_doca = docaify_fwd(fwd_miss);

        doca_flow_pipe *raw_handle;
        enforce_success(doca_flow_pipe_create(
            cfg.handle(),
            &fwd_doca,
            &fwd_miss_doca,
            &raw_handle
        )); 
        handle_.reset(raw_handle);
    }

    auto flow_pipe::add_entry(
        std::uint16_t pipe_queue,
        doca_flow_match const &match,
        std::optional<doca_flow_actions> actions,
        std::optional<doca_flow_monitor> monitor,
        flow_fwd fwd,
        std::uint32_t flags,
        void *usr_ctx
    ) -> flow_pipe_entry {
        doca_flow_pipe_entry *entry_handle;
        auto fwd_doca = docaify_fwd(fwd);

        enforce_success(doca_flow_pipe_add_entry(
            pipe_queue,
            handle(),
            &match,
            actions.has_value() ? &*actions : nullptr,
            monitor.has_value() ? &*monitor : nullptr,
            &fwd_doca,
            flags,
            usr_ctx,
            &entry_handle
        ));

        return { entry_handle };
    }
}
