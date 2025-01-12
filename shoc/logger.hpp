#pragma once

#include <doca_log.h>
#include <spdlog/spdlog.h>
#include <memory>

namespace shoc {
    extern std::shared_ptr<spdlog::logger> const logger;

    auto set_sdk_log_level(doca_log_level level) -> void;
}
