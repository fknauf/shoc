#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"

#include <doca_dma.h>

#include <string>

namespace shoc {
    class dma_context:
        public context<
            doca_dma,
            doca_dma_destroy,
            doca_dma_as_ctx
        >
    {
    public:
        dma_context(
            context_parent *parent,
            device dev,
            std::uint32_t max_tasks
        );

        auto memcpy(
            buffer const &src,
            buffer &dest
        ) const -> coro::status_awaitable<>;

    private:
        device dev_;
    };
}
