#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace doca {
    extern std::shared_ptr<spdlog::logger> const logger;
}
