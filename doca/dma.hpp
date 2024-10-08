#pragma once

#include "buffer.hpp"
#include "common/status.hpp"
#include "context.hpp"
#include "coro/value_awaitable.hpp"
#include "device.hpp"
#include "unique_handle.hpp"

#include <doca_dma.h>

#include <string>

namespace doca {
    class dma_context:
        public context
    {
    public:
        dma_context(
            context_parent *parent,
            device const &dev,
            std::uint32_t max_tasks
        );

        auto memcpy(
            buffer const &src,
            buffer &dest
        ) const -> status_awaitable;

    private:
        unique_handle<doca_dma> handle_ { doca_dma_destroy };
    };
}
