#include "doca/buffer.hpp"
#include "doca/buffer_inventory.hpp"
#include "doca/comch_client.hpp"
#include "doca/logger.hpp"
#include "doca/memory_map.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>
#include <string_view>

int main() {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::debug);    
    
    auto dev = doca::comch_device { "81:00.0" };
    auto engine = doca::progress_engine {};

    auto memory = std::vector<char>(1024);
    auto mmap = doca::memory_map { dev, memory, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = doca::buffer_inventory { 1 };
    auto buffer = bufinv.buf_get_by_addr(mmap, memory);
    doca::comch_client *client;

    doca::comch_consumer_callbacks consumer_callbacks = {
        .state_changed = [&] (
            doca::comch_consumer &self,
            [[maybe_unused]] doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) {
            if(next_state == DOCA_CTX_STATE_RUNNING) {
                self.post_recv_msg(buffer);
            }
        },
        .post_recv_completion = [&] (
            [[maybe_unused]] doca::comch_consumer &self,
            doca::comch_consumer_task_post_recv &task
        ) {
            auto data = task.buf().data();
            std::cout << std::string_view { begin(data), end(data) } << std::endl;
            client->stop();
        },
        .post_recv_error = [&] (
            [[maybe_unused]] doca::comch_consumer &self,
            doca::comch_consumer_task_post_recv &task
        ) {
            auto status = task.status();
            auto name = doca_error_get_name(status);
            auto description = doca_error_get_descr(status);

            doca::logger->error("post_recv error {}: {}", name, description);
            client->stop();
        }
    };

    doca::comch_client_callbacks client_callbacks = {
        .state_changed = [&](
            doca::comch_client &self,
            [[maybe_unused]] doca_ctx_states prev_state,
            doca_ctx_states next_state
        ) {
            if(next_state == DOCA_CTX_STATE_RUNNING) {
                self.submit_message("start");
            }
        },
        .message_received = [&](
            doca::comch_client &self,
            std::span<std::uint8_t> msgbuf,
            [[maybe_unused]] doca_comch_connection *con
        ) {
            auto msg = std::string_view{ reinterpret_cast<char const*>(msgbuf.data()), msgbuf.size() };
            std::cout << msg << std::endl;

            if(msg == "ready") {
                self.create_consumer(mmap, 32, consumer_callbacks);
            }
        }
    };

    client = engine.create_context<doca::comch_client>("vss-data-test", dev, client_callbacks);

    doca::logger->info("sent message");

    engine.main_loop();

    doca::logger->info("done");
}
