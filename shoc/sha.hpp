#pragma once

#include "buffer.hpp"
#include "context.hpp"
#include "coro/status_awaitable.hpp"
#include "device.hpp"

#include <doca_sha.h>

namespace shoc {
    class sha_context:
        public context<
            doca_sha,
            doca_sha_destroy,
            doca_sha_as_ctx
        >
    {
    public:
        sha_context(
            context_parent *parent,
            device dev,
            std::uint32_t max_tasks = 16
        );

        auto hash(
            doca_sha_algorithm algorithm,
            buffer const &src_buf,
            buffer &dst_buf
        ) -> coro::status_awaitable<>;

        auto partial_hash(
            doca_sha_algorithm algorithm,
            buffer const &src_buf,
            buffer &dst_buf,
            bool final_segment
        ) -> coro::status_awaitable<>;
    
    private:
        device dev_;
    };
}