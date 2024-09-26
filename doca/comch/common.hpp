#include <doca/coro/receptable.hpp>

#include <cstdint>
#include <string>
#include <optional>

#include <doca_comch.h>

namespace doca::comch {
    using message = std::optional<std::string>;
    using status_awaitable = coro::receptable_awaiter<doca_error_t>;
    using message_awaitable = coro::receptable_awaiter<message>;
}
