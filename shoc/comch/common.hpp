#pragma once

#include <shoc/common/status.hpp>
#include <shoc/common/accepter_queues.hpp>
#include <shoc/coro/value_awaitable.hpp>

#include <cstdint>
#include <string>

#include <doca_comch.h>

namespace shoc::comch {
    using message = std::string;

    using message_awaitable = coro::value_awaitable<message>;
    using id_awaitable = coro::value_awaitable<std::uint32_t>;

    enum class connection_state {
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED
    };
}
