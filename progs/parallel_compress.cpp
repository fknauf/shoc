#include "doca/buffer_inventory.hpp"
#include "doca/compress.hpp"
#include "doca/logger.hpp"
#include "doca/memory_map.hpp"
#include "doca/progress_engine.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ranges>

#include <fmt/core.h>

#include <doca_log.h>

auto compress_file(std::istream &in, std::ostream &out) {
    std::uint32_t batches;
    std::uint32_t batchsize;
    std::uint32_t const parallelism = 32;

    in.read(reinterpret_cast<char *>(&batches), sizeof batches);
    in.read(reinterpret_cast<char *>(&batchsize), sizeof batchsize);

    doca::logger->info("compressing {} batches of size {}", batches, batchsize);

    auto filesize = batches * batchsize;
    std::vector<char> src_data(filesize);
    std::vector<char> dst_data(filesize);

    in.read(src_data.data(), filesize);

    out.write(reinterpret_cast<char const *>(&batches), sizeof batches);
    out.write(reinterpret_cast<char const *>(&batchsize), sizeof batchsize);

    auto dev = doca::compress_device{};
    auto mmap_src = doca::memory_map { dev, src_data };
    auto mmap_dst = doca::memory_map { dev, dst_data };
    auto engine = doca::progress_engine {};
    auto buf_inv = doca::buffer_inventory { batches * 2 };

    auto src_buffers = std::vector<doca::buffer>{};
    auto dst_buffers = std::vector<doca::buffer>{};

    src_buffers.reserve(batches);
    dst_buffers.reserve(batches);

    std::chrono::time_point<std::chrono::steady_clock> start;
    std::chrono::time_point<std::chrono::steady_clock> end;

    for(auto i : std::ranges::views::iota(0u, batches)) {
        auto offset = i * batchsize;

        src_buffers.push_back(buf_inv.buf_get_by_data(mmap_src, src_data.data() + offset, batchsize));
        dst_buffers.push_back(buf_inv.buf_get_by_addr(mmap_dst, dst_data.data() + offset, batchsize));
    }

    engine.create_context<doca::compress_context>(
        dev,
        (doca::compress_callbacks) {
            .state_changed = [&](
                doca::compress_context &self,
                doca_ctx_states prev_state,
                doca_ctx_states next_state
            ) {
                doca::logger->debug("compress changed state {} -> {}", prev_state, next_state);

                if(next_state == DOCA_CTX_STATE_RUNNING) {
                    start = std::chrono::steady_clock::now();

                    for(auto i : std::ranges::views::iota(0u, parallelism)) {
                        self.compress(src_buffers[i], dst_buffers[i], { .u64 = i });
                    }
                }
            },
            .compress_completed = [&](
                doca::compress_context &self,
                doca::compress_task_compress_deflate &task
            ) {
                doca::logger->debug("compress chunk complete: {}, crc = {}, adler = {}",
                    task.user_data().u64,
                    task.crc_cs(),
                    task.adler_cs()
                );

                auto next_chunk_no = task.user_data().u64 + parallelism;

                if(next_chunk_no < batches) {
                    self.compress(
                        src_buffers[next_chunk_no],
                        dst_buffers[next_chunk_no],
                        { .u64 = next_chunk_no }
                    );
                } else if(self.inflight_tasks() == 0) {
                    end = std::chrono::steady_clock::now();
                    self.stop();
                }
            },
            .compress_error = [&](
                doca::compress_context &self,
                doca::compress_task_compress_deflate &task
            ) {
                doca::logger->error("compression error on chunk {}: Code {}, {}", 
                    task.user_data().u64,
                    doca_error_get_name(task.status()),
                    doca_error_get_descr(task.status())
                );
                self.stop();
            }
        },
        parallelism * 2
    );

    engine.main_loop();

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    doca::logger->info("elapsed time: {} us", elapsed.count());

    for(auto &buf : dst_buffers) {
        auto data = buf.data();
        std::uint32_t size = data.size();

        out.write(reinterpret_cast<char const*>(&size), sizeof size);
        out.write(data.data(), data.size());
    }
}

auto main(int argc, char *argv[]) -> int try {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    doca::logger->set_level(spdlog::level::info);

    if(argc < 3) {
        std::cerr << "Usage: " << argv[0] << " INFILE OUTFILE\n";
        return -1;
    }

    auto in  = std::ifstream(argv[1], std::ios::binary);
    auto out = std::ofstream(argv[2], std::ios::binary);

    compress_file(in, out);
} catch(doca::doca_exception &ex) {
    doca::logger->error("ecode = {}, message = {}", ex.doca_error(), ex.what());
}
