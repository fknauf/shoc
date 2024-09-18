#include "doca/buffer.hpp"
#include "doca/buffer_inventory.hpp"
#include "doca/comch_producer.hpp"
#include "doca/comch_server.hpp"
#include "doca/logger.hpp"
#include "doca/memory_map.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main() {
    auto dev = doca::comch_device { "03:00.0" };
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto engine = doca::progress_engine{};

    auto memory = std::vector<char>(1024, 'x');
    auto mmap = doca::memory_map { dev, memory, DOCA_ACCESS_FLAG_PCI_READ_WRITE };
    auto bufinv = doca::buffer_inventory { 1 };
    auto buffer = bufinv.buf_get_by_data(mmap, memory);

    doca::comch_server_callbacks server_callbacks = {
        .message_received = [&](
            doca::comch_server &self,
            std::span<std::uint8_t> data_range,
            doca_comch_connection *comch_connection
        ) {
            auto msg = std::string_view { reinterpret_cast<char const*>(data_range.data()), data_range.size() };
            if(msg == "start") {
                self.send_response(comch_connection, "ready");
            }
        },
        .new_consumer = [&](
            doca::comch_server &self,
            doca_comch_connection *comch_connection,
            std::uint32_t remote_consumer_id
        ) {
            doca::comch_producer_callbacks producer_callbacks {
                .state_changed = [&, remote_consumer_id]( 
                    doca::comch_producer &self,
                    [[maybe_unused]] doca_ctx_states prev_state,
                    doca_ctx_states next_state
                ) {
                    if(next_state == DOCA_CTX_STATE_RUNNING) {
                        self.send(buffer, {}, remote_consumer_id);
                    }
                },
                .send_completion = [&](
                    doca::comch_producer &self,
                    [[maybe_unused]] doca_comch_producer_task_send *task,
                    [[maybe_unused]] doca_data task_user_data
                ) {
                    self.stop();
                },
                .send_error = [&](
                    doca::comch_producer &self,
                    [[maybe_unused]] doca_comch_producer_task_send *task,
                    [[maybe_unused]] doca_data task_user_data
                ) {
                    auto status = doca_task_get_status(doca_comch_producer_task_send_as_task(task));
                    auto name = doca_error_get_name(status);
                    auto description = doca_error_get_descr(status);

                    doca::logger->error("send error {}: {}", name, description);
                    self.stop();
                }
            };

            self.create_producer<doca::comch_producer>(comch_connection, 32, std::move(producer_callbacks));
        }
    };

    engine.create_context<doca::comch_server>("vss-data-test", dev, rep, server_callbacks);

    engine.main_loop();
}