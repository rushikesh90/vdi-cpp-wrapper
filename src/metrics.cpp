#include "metrics.h"
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
      hist_under_100us_(0),
      hist_100us_1ms_(0),
      hist_1ms_10ms_(0),
      hist_over_10ms_(0),
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

void Metrics::record_chunk_latency(
    std::chrono::microseconds latency) {

    chunk_latencies_.push_back(
        static_cast<uint64_t>(latency.count()));
}

void Metrics::record_getcommand_latency(
    std::chrono::microseconds latency) {

    getcommand_latencies_.push_back(
        static_cast<uint64_t>(latency.count()));
}

void Metrics::record_completecommand_latency(
    std::chrono::microseconds latency) {

    completecommand_latencies_.push_back(
        static_cast<uint64_t>(latency.count()));
}

void Metrics::record_sink_latency(
    std::chrono::microseconds latency) {

    sink_latencies_.push_back(
        static_cast<uint64_t>(latency.count()));
}

void Metrics::record_latency_histogram(uint64_t us) {
    if (us < 100) {
        hist_under_100us_++;
    } else if (us < 1000) {
        hist_100us_1ms_++;
    } else if (us < 10000) {
        hist_1ms_10ms_++;
    } else {
        hist_over_10ms_++;
    }
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
    // p=50 → rank = ceil(50 / 100 * N) → 1-based index
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
    // FILETIME is in 100-ns intervals; divide by 10 → microseconds
    return ui.QuadPart / 10;
}

// Helper: percentile over a ChunkTiming vector extracting one field
template<typename Getter>
static uint64_t timing_percentile(
    const std::vector<ChunkTiming>& timings,
    double p,
    Getter getter) {

    if (timings.empty()) {
        return 0;
    }

    std::vector<uint64_t> extracted;
    extracted.reserve(timings.size());
    for (const auto& t : timings) {
        extracted.push_back(getter(t));
    }
    std::sort(extracted.begin(), extracted.end());

    double rank = std::ceil(p / 100.0 * static_cast<double>(extracted.size()));
    size_t idx = static_cast<size_t>(rank) - 1;
    if (idx >= extracted.size()) {
        idx = extracted.size() - 1;
    }
    return extracted[idx];
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

    // --- Aggregate stats ---
    std::cout << "Bytes: "
              << total_bytes_ << "\n";

    std::cout << "Chunks: "
              << chunk_count_ << "\n";

    // --- Chunk size stats ---
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

    // Average chunk size (legacy line)
    if (chunk_count_ > 0) {
        uint64_t avg = total_bytes_ / chunk_count_;
        std::cout << "\nAverage chunk size (from totals): "
                  << avg << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (static_cast<double>(avg) / 1024.0)
                  << " KB)\n";
    }

    // Total elapsed time
    std::cout << "\nTotal elapsed time: "
              << std::fixed << std::setprecision(1)
              << seconds << " s\n";

    // CPU utilization (Windows only)
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

        auto print_stage = [&](const char* name, auto getter) {
            std::cout << std::right
                      << std::setw(12) << name
                      << std::setw(12) << timing_percentile(timings_, 50, getter)
                      << std::setw(12) << timing_percentile(timings_, 95, getter)
                      << std::setw(12) << timing_percentile(timings_, 99, getter)
                      << "\n";
        };

        print_stage("GetCommand", [](const ChunkTiming& t) { return t.getcommand_us; });
        print_stage("Dispatch",   [](const ChunkTiming& t) { return t.dispatch_us; });
        print_stage("Sink",       [](const ChunkTiming& t) { return t.sink_us; });
        print_stage("Complete",   [](const ChunkTiming& t) { return t.complete_us; });
        print_stage("Total",      [](const ChunkTiming& t) { return t.total_us; });
        std::cout << "\n";
    }

    // --- GetCommand Latency (legacy) ---
    std::cout << "\nGetCommand Latency:\n";
    if (getcommand_latencies_.empty()) {
        std::cout << "  (no samples)\n";
    } else {
        std::cout << "  P50: "
                  << percentile(getcommand_latencies_, 50)
                  << " us\n";
        std::cout << "  P95: "
                  << percentile(getcommand_latencies_, 95)
                  << " us\n";
        std::cout << "  P99: "
                  << percentile(getcommand_latencies_, 99)
                  << " us\n";
    }

    // --- CompleteCommand Latency (new) ---
    std::cout << "\nCompleteCommand Latency:\n";
    if (completecommand_latencies_.empty()) {
        std::cout << "  (no samples)\n";
    } else {
        std::cout << "  P50: "
                  << percentile(completecommand_latencies_, 50)
                  << " us\n";
        std::cout << "  P95: "
                  << percentile(completecommand_latencies_, 95)
                  << " us\n";
        std::cout << "  P99: "
                  << percentile(completecommand_latencies_, 99)
                  << " us\n";
    }

    // --- Sink Write Latency (new) ---
    std::cout << "\nSink Write Latency:\n";
    if (sink_latencies_.empty()) {
        std::cout << "  (no samples)\n";
    } else {
        std::cout << "  P50: "
                  << percentile(sink_latencies_, 50)
                  << " us\n";
        std::cout << "  P95: "
                  << percentile(sink_latencies_, 95)
                  << " us\n";
        std::cout << "  P99: "
                  << percentile(sink_latencies_, 99)
                  << " us\n";
    }

    // --- Chunk Processing Latency (legacy) ---
    std::cout << "\nChunk Processing (legacy):\n";
    if (chunk_latencies_.empty()) {
        std::cout << "  (no samples)\n";
    } else {
        std::cout << "  P50: "
                  << percentile(chunk_latencies_, 50)
                  << " us\n";
        std::cout << "  P95: "
                  << percentile(chunk_latencies_, 95)
                  << " us\n";
        std::cout << "  P99: "
                  << percentile(chunk_latencies_, 99)
                  << " us\n";
    }

    // --- Latency Histogram ---
    {
        uint64_t under_100 = hist_under_100us_.load();
        uint64_t _100_1ms  = hist_100us_1ms_.load();
        uint64_t _1ms_10ms = hist_1ms_10ms_.load();
        uint64_t over_10ms = hist_over_10ms_.load();
        uint64_t total_samples = under_100 + _100_1ms + _1ms_10ms + over_10ms;

        std::cout << "\nLatency Histogram (chunk processing):\n";
        if (total_samples == 0) {
            std::cout << "  (no samples)\n";
        } else {
            auto pct = [total_samples](uint64_t count) -> double {
                return (static_cast<double>(count) / static_cast<double>(total_samples)) * 100.0;
            };
            std::cout << "  <100 us       "
                      << std::right << std::setw(8) << under_100 << "  "
                      << std::fixed << std::setprecision(1) << pct(under_100) << "%\n";
            std::cout << "  100 us-1 ms   "
                      << std::right << std::setw(8) << _100_1ms << "  "
                      << std::fixed << std::setprecision(1) << pct(_100_1ms) << "%\n";
            std::cout << "  1 ms-10 ms    "
                      << std::right << std::setw(8) << _1ms_10ms << "  "
                      << std::fixed << std::setprecision(1) << pct(_1ms_10ms) << "%\n";
            std::cout << "  >10 ms        "
                      << std::right << std::setw(8) << over_10ms << "  "
                      << std::fixed << std::setprecision(1) << pct(over_10ms) << "%\n";
        }
    }

    // --- Buffer Pool Utilization ---
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

    // --- Logging status ---
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

    uint64_t p50_gc = percentile(getcommand_latencies_, 50);
    uint64_t p95_gc = percentile(getcommand_latencies_, 95);
    uint64_t p99_gc = percentile(getcommand_latencies_, 99);

    uint64_t p50_cc = percentile(completecommand_latencies_, 50);
    uint64_t p95_cc = percentile(completecommand_latencies_, 95);
    uint64_t p99_cc = percentile(completecommand_latencies_, 99);

    uint64_t p50_sink = percentile(sink_latencies_, 50);
    uint64_t p95_sink = percentile(sink_latencies_, 95);
    uint64_t p99_sink = percentile(sink_latencies_, 99);

    uint64_t p50_chunk = percentile(chunk_latencies_, 50);
    uint64_t p95_chunk = percentile(chunk_latencies_, 95);
    uint64_t p99_chunk = percentile(chunk_latencies_, 99);

    // Per-stage latencies from ChunkTiming vectors
    auto stage_p50 = [&](auto getter) { return timing_percentile(timings_, 50, getter); };
    auto stage_p95 = [&](auto getter) { return timing_percentile(timings_, 95, getter); };
    auto stage_p99 = [&](auto getter) { return timing_percentile(timings_, 99, getter); };

    uint64_t under_100 = hist_under_100us_.load();
    uint64_t _100_1ms  = hist_100us_1ms_.load();
    uint64_t _1ms_10ms = hist_1ms_10ms_.load();
    uint64_t over_10ms = hist_over_10ms_.load();
    uint64_t total_hist = under_100 + _100_1ms + _1ms_10ms + over_10ms;

    uint64_t acquires = buffer_acquire_count_.load();
    uint64_t hits     = buffer_pool_hits_.load();
    double reuse_rate = (acquires > 0)
        ? (static_cast<double>(hits) / static_cast<double>(acquires)) * 100.0
        : 0.0;

    std::ostringstream os;
    os << "{\n";
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
    os << "  \"getcommand_latency_us\": {\n";
    os << "    \"p50\": " << p50_gc << ",\n";
    os << "    \"p95\": " << p95_gc << ",\n";
    os << "    \"p99\": " << p99_gc << "\n";
    os << "  },\n";
    os << "  \"completecommand_latency_us\": {\n";
    os << "    \"p50\": " << p50_cc << ",\n";
    os << "    \"p95\": " << p95_cc << ",\n";
    os << "    \"p99\": " << p99_cc << "\n";
    os << "  },\n";
    os << "  \"sink_latency_us\": {\n";
    os << "    \"p50\": " << p50_sink << ",\n";
    os << "    \"p95\": " << p95_sink << ",\n";
    os << "    \"p99\": " << p99_sink << "\n";
    os << "  },\n";
    os << "  \"chunk_latency_us\": {\n";
    os << "    \"p50\": " << p50_chunk << ",\n";
    os << "    \"p95\": " << p95_chunk << ",\n";
    os << "    \"p99\": " << p99_chunk << "\n";
    os << "  },\n";
    os << "  \"per_stage_timing_us\": {\n";
    os << "    \"getcommand\": {\n";
    os << "      \"p50\": " << stage_p50([](const ChunkTiming& t) { return t.getcommand_us; }) << ",\n";
    os << "      \"p95\": " << stage_p95([](const ChunkTiming& t) { return t.getcommand_us; }) << ",\n";
    os << "      \"p99\": " << stage_p99([](const ChunkTiming& t) { return t.getcommand_us; }) << "\n";
    os << "    },\n";
    os << "    \"dispatch\": {\n";
    os << "      \"p50\": " << stage_p50([](const ChunkTiming& t) { return t.dispatch_us; }) << ",\n";
    os << "      \"p95\": " << stage_p95([](const ChunkTiming& t) { return t.dispatch_us; }) << ",\n";
    os << "      \"p99\": " << stage_p99([](const ChunkTiming& t) { return t.dispatch_us; }) << "\n";
    os << "    },\n";
    os << "    \"sink\": {\n";
    os << "      \"p50\": " << stage_p50([](const ChunkTiming& t) { return t.sink_us; }) << ",\n";
    os << "      \"p95\": " << stage_p95([](const ChunkTiming& t) { return t.sink_us; }) << ",\n";
    os << "      \"p99\": " << stage_p99([](const ChunkTiming& t) { return t.sink_us; }) << "\n";
    os << "    },\n";
    os << "    \"complete\": {\n";
    os << "      \"p50\": " << stage_p50([](const ChunkTiming& t) { return t.complete_us; }) << ",\n";
    os << "      \"p95\": " << stage_p95([](const ChunkTiming& t) { return t.complete_us; }) << ",\n";
    os << "      \"p99\": " << stage_p99([](const ChunkTiming& t) { return t.complete_us; }) << "\n";
    os << "    },\n";
    os << "    \"total\": {\n";
    os << "      \"p50\": " << stage_p50([](const ChunkTiming& t) { return t.total_us; }) << ",\n";
    os << "      \"p95\": " << stage_p95([](const ChunkTiming& t) { return t.total_us; }) << ",\n";
    os << "      \"p99\": " << stage_p99([](const ChunkTiming& t) { return t.total_us; }) << "\n";
    os << "    }\n";
    os << "  },\n";
    os << "  \"latency_histogram\": {\n";
    os << "    \"under_100us\": " << under_100 << ",\n";
    os << "    \"100us_1ms\": " << _100_1ms << ",\n";
    os << "    \"1ms_10ms\": " << _1ms_10ms << ",\n";
    os << "    \"over_10ms\": " << over_10ms << ",\n";
    os << "    \"total_samples\": " << total_hist << "\n";
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