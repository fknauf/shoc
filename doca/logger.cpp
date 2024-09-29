#include "logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <doca_log.h>

namespace doca {
    std::shared_ptr<spdlog::logger> const logger = spdlog::stderr_color_mt("docapp");

    auto set_sdk_log_level(doca_log_level level) -> void {
        doca_log_backend *sdk_log;

        doca_log_backend_create_standard();
        doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
        doca_log_backend_set_sdk_level(sdk_log, level);
    }
}
