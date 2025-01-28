#pragma once

#include <asio/awaitable.hpp>

namespace shoc::coro {
    using fiber = asio::awaitable<void>;
}
