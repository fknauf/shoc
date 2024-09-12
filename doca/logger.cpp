#include "logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace doca {
    std::shared_ptr<spdlog::logger> const logger = spdlog::stderr_color_mt("docapp");
}
