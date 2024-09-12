#include "doca/comch_server.hpp"
#include "doca/progress_engine.hpp"

#include <iostream>
#include <string_view>

int main() {
    auto dev = doca::comch_device { "03:00.0" };
    auto rep = doca::device_representor::find_by_pci_addr ( dev, "81:00.0" );

    auto engine = doca::progress_engine{};
    
    engine.create_context<doca::comch_server>(
        "vss-test", dev, rep,
        (doca::comch_server_callbacks) {
            .message_received = [&](
                doca::comch_server &server,
                std::span<std::uint8_t> msg_buf,
                doca_comch_connection *con
            ) {
                auto msg = std::string_view(reinterpret_cast<char const *>(msg_buf.data()), msg_buf.size());
                std::cout << msg << std::endl;

                server.send_response(con, "pong");
            }
        }
    );

    engine.main_loop();
}