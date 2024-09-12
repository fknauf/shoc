#include "doca/comch_client.hpp"
#include "doca/logger.hpp"
#include "doca/progress_engine.hpp"

#include <latch>
#include <iostream>

int main() {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::debug);    
    
    auto dev = doca::comch_device { "81:00.0" };
    auto engine = doca::progress_engine {};

    engine.create_context<doca::comch_client>(
        "vss-test",
        dev, 
        (doca::comch_client_callbacks) {
            .state_changed = [&](
                doca::comch_client &client,
                [[maybe_unused]] doca_ctx_states prev_state,
                doca_ctx_states next_state
            ) {
                if(next_state == DOCA_CTX_STATE_RUNNING) {
                    client.submit_message("Hello, world.");
                }
            },
            .message_received = [&](
                doca::comch_client &client,
                std::span<std::uint8_t> msgbuf,
                [[maybe_unused]] doca_comch_connection *con
            ) {
                auto msg = std::string_view{ reinterpret_cast<char const*>(msgbuf.data()), msgbuf.size() };
                std::cout << msg << std::endl;

                client.stop();
            }
        }
    );

    doca::logger->info("sent message");

    engine.main_loop();

    doca::logger->info("done");
}
