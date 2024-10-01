#pragma once

#include "common.hpp"
#include "consumer.hpp"
#include "device.hpp"
#include "producer.hpp"

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

    /**
     * comch client. This is conceptually almost the same as a server_connection, except that we're the ones
     * asking for it.
     *
     * When the client is started, it is associated with a connection, and all messages, child consumers and producers
     * are likewise associated with that connection.
     */
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

        /**
         * Send message to the server we're connected to
         *
         * @return awaitable for the send status
         */
        auto send(std::string_view message) -> status_awaitable;

        /**
         * Receive/wait for a message from the server
         *
         * @return awaitable for a message from the server
         */
        auto msg_recv() -> message_awaitable;

        /**
         * Create a consumer to receive data buffers from a producer on the server side
         *
         * @param user_mmap memory map for the buffers where we receive data
         * @param max_tasks maximum number of tasks that can be in flight on the same time on this consumer
         */
        auto create_consumer(memory_map &user_mmap, std::uint32_t max_tasks){
            return active_children_.create_context<consumer>(this, connection_handle(), user_mmap, max_tasks);
        }

        /**
         * Create a producer to send data buffers to a consumer on the server side
         *
         * @param max_tasks maximum number of parallel in-flight tasks
         */
        auto create_producer(std::uint32_t max_tasks){
            return active_children_.create_context<producer>(this, connection_handle(), max_tasks);
        }

        /**
         * Accept a server-side consumer and retrieve its ID, or wait for one to appear
         *
         * @return awaitable to co_await a remote consumer id
         */
        auto accept_consumer() -> id_awaitable {
            return remote_consumer_queues_.accept();
        }

        auto stop() -> context_state_awaitable override;

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

        static auto new_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        static auto expired_consumer_entry(
            doca_comch_event_consumer *event,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) -> void;

        static auto resolve(doca_comch_connection*) -> client*;
        static auto resolve(doca_comch_client*) -> client*;
        auto connection_handle() const -> doca_comch_connection*;
        auto signal_stopped_child(context *child) -> void override;
        auto disconnect_if_able() -> void;

        unique_handle<doca_comch_client> handle_ { doca_comch_client_destroy };
        connection_state state_ = connection_state::DISCONNECTED;

        accepter_queues<message> message_queues_;
        accepter_queues<std::uint32_t> remote_consumer_queues_;

        dependent_contexts<> active_children_;
    };
}
