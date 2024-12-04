#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
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
            device dev,
            std::uint32_t max_tasks
        );

        auto memcpy(
            buffer const &src,
            buffer &dest
        ) const -> coro::status_awaitable<>;

        auto as_ctx() const noexcept -> doca_ctx* override {
            return doca_dma_as_ctx(handle_.handle());
        }

    private:
        device dev_;
        unique_handle<doca_dma, doca_dma_destroy> handle_;
    };
}
