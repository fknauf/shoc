#include "doca/buffer_inventory.hpp"
#include "doca/buffer_pool.hpp"
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

    in.read(reinterpret_cast<char *>(&batches), sizeof batches);
    in.read(reinterpret_cast<char *>(&batchsize), sizeof batchsize);

    doca::logger->info("compressing {} batches of size {}", batches, batchsize);

    out.write(reinterpret_cast<char const *>(&batches), sizeof batches);
    out.write(reinterpret_cast<char const *>(&batchsize), sizeof batchsize);

    auto dev = doca::compress_device{};
    auto engine = doca::progress_engine {};
    auto buf_pool = doca::buffer_pool{dev, 2, batchsize};
    auto src_buf = buf_pool.allocate_buffer(batchsize);
    auto dst_buf = buf_pool.allocate_buffer();

    auto src_data = src_buf.data();

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
                    self.compress(src_buf, dst_buf, { .u64 = 0 });
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

                auto dst_data = task.dst().data();

                std::uint32_t dst_size = dst_data.size();
                out.write(reinterpret_cast<char const *>(&dst_size), sizeof(dst_size));
                out.write(dst_data.data(), dst_data.size());

                auto next_chunk_no = task.user_data().u64 + 1;

                if(next_chunk_no < batches) {
                    dst_buf.set_data(0);
                    in.read(src_data.data(), src_data.size());
                    self.compress(src_buf, dst_buf, { .u64 = next_chunk_no });
                } else {
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
        16
    );

    in.read(src_data.data(), src_data.size());

    engine.main_loop();
}

auto main(int argc, char *argv[]) -> int try {
    doca_log_backend *sdk_log;

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_DEBUG);

    doca::logger->set_level(spdlog::level::debug);

    if(argc < 3) {
        std::cerr << "Usage: " << argv[0] << " INFILE OUTFILE\n";
        return -1;
    }

    auto in  = std::ifstream(argv[1], std::ios::binary);
    auto out = std::ofstream(argv[2], std::ios::binary);

    auto start = std::chrono::steady_clock::now();

    compress_file(in, out);

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    doca::logger->info("elapsed time: {} us", elapsed.count());
} catch(doca::doca_exception &ex) {
    doca::logger->error("ecode = {}, message = {}", ex.doca_error(), ex.what());
}
