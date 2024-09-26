#include <doca/coro/value_awaitable.hpp>

#include <cstdint>
#include <string>
#include <optional>

#include <doca_comch.h>

namespace doca::comch {
    using message = std::optional<std::string>;
    using status_awaitable = coro::value_awaitable<doca_error_t>;
    using message_awaitable = coro::value_awaitable<message>;
}
