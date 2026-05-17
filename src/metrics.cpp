#include "metrics.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

Metrics::Metrics(std::unique_ptr<Timer> timer, bool enable_logging)
    : timer_(std::move(timer)),
      logging_enabled_(enable_logging),
      total_bytes_(0),
      chunk_count_(0),
      min_chunk_size_(UINT64_MAX),
      max_chunk_size_(0),
      sum_chunk_sizes_(0),
      buffer_acquire_count_(0),
      buffer_release_count_(0),
      buffer_pool_hits_(0),
      buffer_pool_misses_(0),
      start_time_(
          std::chrono::steady_clock::now()) {}

void Metrics::add_bytes(size_t size) {
    total_bytes_ += size;
    chunk_count_++;
}

void Metrics::record_chunk_size(size_t size) {
    // Update min (atomically if smaller)
    uint64_t cur_min = min_chunk_size_.load();
    while (static_cast<uint64_t>(size) < cur_min) {
        if (min_chunk_size_.compare_exchange_weak(cur_min, size)) {
            break;
        }
    }

    // Update max (atomically if larger)
    uint64_t cur_max = max_chunk_size_.load();
    while (static_cast<uint64_t>(size) > cur_max) {
        if (max_chunk_size_.compare_exchange_weak(cur_max, size)) {
            break;
        }
    }

    sum_chunk_sizes_ += size;
}

void Metrics::record_chunk_timing(const ChunkTiming& timing) {
    timings_.push_back(timing);
}

void Metrics::record_buffer_acquire(bool success) {
    buffer_acquire_count_++;
    if (success) {
        buffer_pool_hits_++;
    } else {
        buffer_pool_misses_++;
    }
}

void Metrics::record_buffer_release() {
    buffer_release_count_++;
}

std::vector<uint64_t> Metrics::stage_values(
    uint64_t ChunkTiming::*field) const
{
    std::vector<uint64_t> values;
    values.reserve(timings_.size());
    for (const auto& t : timings_) {
        values.push_back(t.*field);
    }
    return values;
}

uint64_t Metrics::percentile(
    const std::vector<uint64_t>& values,
    double p) {

    if (values.empty()) {
        return 0;
    }

    // Sort a copy so the original order is preserved
    std::vector<uint64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    // Rank using nearest-rank method
    // p=50 => rank = ceil(50 / 100 * N) => 1-based index
    double rank = std::ceil(p / 100.0 * static_cast<double>(sorted.size()));
    size_t idx = static_cast<size_t>(rank) - 1;

    if (idx >= sorted.size()) {
        idx = sorted.size() - 1;
    }

    return sorted[idx];
}

// Helper: convert FILETIME to 64-bit microseconds
static uint64_t filetime_to_us(const FILETIME& ft) {
    ULARGE_INTEGER ui;
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    // FILETIME is in 100-ns intervals; divide by 10 => microseconds
    return ui.QuadPart / 10;
}

void Metrics::print_summary() const {

    auto end =
        std::chrono::steady_clock::now();

    double seconds =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                end - start_time_).count() / 1000.0;

    double mb =
        total_bytes_ / 1024.0 / 1024.0;

    double throughput =
        mb / seconds;

    // ── Aggregate stats ──────────────────────────────────────────────────
    std::cout << "Bytes: "
              << total_bytes_ << "\n";

    std::cout << "Chunks: "
              << chunk_count_ << "\n";

    // ── Chunk size stats ─────────────────────────────────────────────────
    std::cout << "\nChunk Sizes:\n";
    if (chunk_count_ > 0) {
        uint64_t cur_min = min_chunk_size_.load();
        uint64_t cur_max = max_chunk_size_.load();
        uint64_t sum = sum_chunk_sizes_.load();

        // If min was never updated (still UINT64_MAX), treat as 0
        if (cur_min == UINT64_MAX) {
            cur_min = 0;
        }

        uint64_t avg = (chunk_count_ > 0) ? (sum / chunk_count_) : 0;

        std::cout << "  Min: " << cur_min << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (static_cast<double>(cur_min) / 1024.0)
                  << " KB)\n";

        std::cout << "  Avg: " << avg << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (static_cast<double>(avg) / 1024.0)
                  << " KB)\n";

        std::cout << "  Max: " << cur_max << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (static_cast<double>(cur_max) / 1024.0)
                  << " KB)\n";
    } else {
        std::cout << "  (no data)\n";
    }

    if (chunk_count_ > 0) {
        uint64_t avg = total_bytes_ / chunk_count_;
        std::cout << "\nAverage chunk size (from totals): "
                  << avg << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (static_cast<double>(avg) / 1024.0)
                  << " KB)\n";
    }

    // ── Total elapsed time ───────────────────────────────────────────────
    std::cout << "\nTotal elapsed time: "
              << std::fixed << std::setprecision(1)
              << seconds << " s\n";

    // ── CPU utilization (Windows only) ───────────────────────────────────
#ifdef _WIN32
    {
        FILETIME creation_time, exit_time, kernel_time, user_time;
        if (GetProcessTimes(GetCurrentProcess(),
                            &creation_time, &exit_time,
                            &kernel_time, &user_time)) {
            uint64_t kernel_us = filetime_to_us(kernel_time);
            uint64_t user_us   = filetime_to_us(user_time);
            uint64_t total_cpu_us = kernel_us + user_us;
            double wall_us = seconds * 1'000'000.0;
            double cpu_pct = (wall_us > 0.0)
                ? (static_cast<double>(total_cpu_us) / wall_us) * 100.0
                : 0.0;
            std::cout << "CPU utilization: "
                      << std::fixed << std::setprecision(1)
                      << cpu_pct << "%"
                      << " (kernel=" << (kernel_us / 1000)
                      << " ms, user=" << (user_us / 1000)
                      << " ms, total=" << (total_cpu_us / 1000)
                      << " ms)\n";
        }
    }
#endif

    std::cout << "\nThroughput MB/s: "
              << std::fixed << std::setprecision(1)
              << throughput << "\n";

    // ── Per-Stage Latency Breakdown (from ChunkTiming vectors) ───────────
    if (!timings_.empty()) {
        std::cout << "\n--- Per-Stage Latency Breakdown (us) ---\n";
        std::cout << std::right
                  << std::setw(12) << "Stage"
                  << std::setw(12) << "P50"
                  << std::setw(12) << "P95"
                  << std::setw(12) << "P99"
                  << "\n";
        std::cout << std::string(48, '-') << "\n";

        auto print_stage = [&](const char* name, uint64_t ChunkTiming::*field) {
            auto values = stage_values(field);
            std::cout << std::right
                      << std::setw(12) << name
                      << std::setw(12) << percentile(values, 50)
                      << std::setw(12) << percentile(values, 95)
                      << std::setw(12) << percentile(values, 99)
                      << "\n";
        };

        print_stage("GetCommand", &ChunkTiming::getcommand_us);
        print_stage("Dispatch",   &ChunkTiming::dispatch_us);
        print_stage("Sink",       &ChunkTiming::sink_us);
        print_stage("Complete",   &ChunkTiming::complete_us);
        print_stage("Total",      &ChunkTiming::total_us);
        std::cout << "\n";
    }

    // ── Buffer Pool Utilization ──────────────────────────────────────────
    {
        uint64_t acquires = buffer_acquire_count_.load();
        uint64_t releases = buffer_release_count_.load();
        uint64_t hits     = buffer_pool_hits_.load();
        uint64_t misses   = buffer_pool_misses_.load();
        double reuse_rate = (acquires > 0)
            ? (static_cast<double>(hits) / static_cast<double>(acquires)) * 100.0
            : 0.0;

        std::cout << "\nBuffer Pool:\n";
        std::cout << "  Acquires:      " << acquires << "\n";
        std::cout << "  Releases:      " << releases << "\n";
        std::cout << "  Hits:          " << hits << "\n";
        std::cout << "  Misses:        " << misses << "\n";
        std::cout << "  Reuse rate:    "
                  << std::fixed << std::setprecision(1)
                  << reuse_rate << "%\n";
    }

    // ── Logging status ───────────────────────────────────────────────────
    if (logging_enabled_) {
        std::cout << "\n* Logging was ON during this run\n";
    }

    std::cout << std::endl;
}

std::string Metrics::to_json() const {
    auto end = std::chrono::steady_clock::now();
    double seconds =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                end - start_time_).count() / 1000.0;
    double mb = total_bytes_ / 1024.0 / 1024.0;
    double throughput = (seconds > 0.0) ? (mb / seconds) : 0.0;

    uint64_t cur_min = min_chunk_size_.load();
    if (cur_min == UINT64_MAX) cur_min = 0;
    uint64_t cur_max = max_chunk_size_.load();
    uint64_t avg = (chunk_count_ > 0) ? (sum_chunk_sizes_.load() / chunk_count_) : 0;

    // CPU times
#ifdef _WIN32
    FILETIME creation_time, exit_time, kernel_time, user_time;
    uint64_t kernel_us = 0, user_us = 0;
    if (GetProcessTimes(GetCurrentProcess(),
                        &creation_time, &exit_time,
                        &kernel_time, &user_time)) {
        kernel_us = filetime_to_us(kernel_time);
        user_us   = filetime_to_us(user_time);
    }
#endif

    // Per-stage latencies from ChunkTiming vectors
    auto stage_p = [&](uint64_t ChunkTiming::*field, double p) -> uint64_t {
        return percentile(stage_values(field), p);
    };

    uint64_t acquires = buffer_acquire_count_.load();
    uint64_t hits     = buffer_pool_hits_.load();
    double reuse_rate = (acquires > 0)
        ? (static_cast<double>(hits) / static_cast<double>(acquires)) * 100.0
        : 0.0;

    std::ostringstream os;
    os << "{\n";
    os << "  \"tool_version\": \"" VDI_WRAPPER_VERSION_STRING "\",\n";
    os << "  \"throughput_mbps\": " << std::fixed << std::setprecision(1) << throughput << ",\n";
    os << "  \"total_bytes\": " << total_bytes_ << ",\n";
    os << "  \"chunk_count\": " << chunk_count_ << ",\n";
    os << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(2) << seconds << ",\n";
    os << "  \"chunk_sizes\": {\n";
    os << "    \"min_bytes\": " << cur_min << ",\n";
    os << "    \"avg_bytes\": " << avg << ",\n";
    os << "    \"max_bytes\": " << cur_max << "\n";
    os << "  },\n";
#ifdef _WIN32
    os << "  \"cpu\": {\n";
    os << "    \"kernel_ms\": " << (kernel_us / 1000) << ",\n";
    os << "    \"user_ms\": " << (user_us / 1000) << ",\n";
    os << "    \"total_ms\": " << ((kernel_us + user_us) / 1000) << "\n";
    os << "  },\n";
#endif
    os << "  \"per_stage_timing_us\": {\n";
    os << "    \"getcommand\": {\n";
    os << "      \"p50\": " << stage_p(&ChunkTiming::getcommand_us, 50) << ",\n";
    os << "      \"p95\": " << stage_p(&ChunkTiming::getcommand_us, 95) << ",\n";
    os << "      \"p99\": " << stage_p(&ChunkTiming::getcommand_us, 99) << "\n";
    os << "    },\n";
    os << "    \"dispatch\": {\n";
    os << "      \"p50\": " << stage_p(&ChunkTiming::dispatch_us, 50) << ",\n";
    os << "      \"p95\": " << stage_p(&ChunkTiming::dispatch_us, 95) << ",\n";
    os << "      \"p99\": " << stage_p(&ChunkTiming::dispatch_us, 99) << "\n";
    os << "    },\n";
    os << "    \"sink\": {\n";
    os << "      \"p50\": " << stage_p(&ChunkTiming::sink_us, 50) << ",\n";
    os << "      \"p95\": " << stage_p(&ChunkTiming::sink_us, 95) << ",\n";
    os << "      \"p99\": " << stage_p(&ChunkTiming::sink_us, 99) << "\n";
    os << "    },\n";
    os << "    \"complete\": {\n";
    os << "      \"p50\": " << stage_p(&ChunkTiming::complete_us, 50) << ",\n";
    os << "      \"p95\": " << stage_p(&ChunkTiming::complete_us, 95) << ",\n";
    os << "      \"p99\": " << stage_p(&ChunkTiming::complete_us, 99) << "\n";
    os << "    },\n";
    os << "    \"total\": {\n";
    os << "      \"p50\": " << stage_p(&ChunkTiming::total_us, 50) << ",\n";
    os << "      \"p95\": " << stage_p(&ChunkTiming::total_us, 95) << ",\n";
    os << "      \"p99\": " << stage_p(&ChunkTiming::total_us, 99) << "\n";
    os << "    }\n";
    os << "  },\n";
    os << "  \"buffer_pool\": {\n";
    os << "    \"acquires\": " << acquires << ",\n";
    os << "    \"releases\": " << buffer_release_count_.load() << ",\n";
    os << "    \"hits\": " << hits << ",\n";
    os << "    \"misses\": " << buffer_pool_misses_.load() << ",\n";
    os << "    \"reuse_rate_pct\": " << std::fixed << std::setprecision(1) << reuse_rate << "\n";
    os << "  },\n";
    os << "  \"logging_enabled\": " << (logging_enabled_ ? "true" : "false") << "\n";
    os << "}";
    return os.str();
}