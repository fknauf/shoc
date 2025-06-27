#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"

#include <doca_dma.h>

#include <string>

/**
 * DOCA DMA functionality, see https://docs.nvidia.com/doca/sdk/doca+dma/index.html
 */
namespace shoc {
    /**
     * Context for DMA-memcpy offloading
     */
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

        /**
         * Offload a task for DMA memcpy. Typically (but not necessarily) one of the buffers
         * is going to be remote (host or DPU) and exported with memory_map::export_pci().
         * 
         * @param src input buffer
         * @param dst output buffer
         */
        auto memcpy(
            buffer const &src,
            buffer &dest
        ) const -> coro::status_awaitable<>;

    private:
        device dev_;
    };
}
