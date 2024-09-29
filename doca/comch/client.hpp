#pragma once

#include "common.hpp"
#include "device.hpp"

#include <doca/context.hpp>
#include <doca/unique_handle.hpp>

#include <doca_comch.h>

#include <queue>
#include <string>
#include <string_view>

namespace doca::comch {
    struct client_limits {
        std::uint32_t num_send_tasks = 1024;
        std::uint32_t max_msg_size = 4080;
        std::uint32_t recv_queue_size = 16;
    };

    class client:
        public context
    {
    public:
        client(
            context_parent *parent,
            std::string const &server_name,
            comch_device &dev,
            client_limits const &limits = {}
        );

        ~client();

        [[nodiscard]] auto as_ctx() const -> doca_ctx* override;

        auto send(std::string_view message) -> status_awaitable;
        auto msg_recv() -> message_awaitable;

    protected:
        auto state_changed(
            doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) -> void override;

    private:
        static auto send_completion_entry(
            doca_comch_task_send *task,
            doca_data task_user_data,
            doca_data ctx_user_data
        ) -> void;

        static auto msg_recv_entry(
            doca_comch_event_msg_recv *event,
            std::uint8_t *recv_buffer,
            std::uint32_t msg_len,
            doca_comch_connection *comch_connection
        ) -> void;

        static auto resolve(doca_comch_connection*) -> client*;
        static auto resolve(doca_comch_client*) -> client*;

        unique_handle<doca_comch_client> handle_ { doca_comch_client_destroy };

        accepter_queues<message> message_queues_;
    };
}
