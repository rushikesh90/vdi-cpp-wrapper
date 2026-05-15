#include "metrics.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

Metrics::Metrics()
    : total_bytes_(0),
      chunk_count_(0),
      start_time_(
          std::chrono::steady_clock::now()) {}

void Metrics::add_bytes(size_t size) {
    total_bytes_ += size;
    chunk_count_++;
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

    // Average chunk size
    if (chunk_count_ > 0) {
        uint64_t avg = total_bytes_ / chunk_count_;
        std::cout << "\nAverage chunk size: "
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
                      << " ms)\n";
        }
    }
#endif

    std::cout << "\nThroughput MB/s: "
              << std::fixed << std::setprecision(1)
              << throughput << "\n";

    // --- GetCommand Latency ---
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

    // --- Chunk Processing Latency ---
    std::cout << "\nChunk Processing:\n";
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

    std::cout << std::endl;
}