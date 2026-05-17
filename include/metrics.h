#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>
#include <memory>

#include "chunk_timing.h"
#include "timer.h"

class Metrics {
public:
    Metrics(std::unique_ptr<Timer> timer = std::make_unique<ChronoTimer>(),
            bool enable_logging = false);

    void add_bytes(size_t size);

    void record_chunk_size(size_t size);

    // Record timing for each chunk individually (full vector storage)
    void record_chunk_timing(const ChunkTiming& timing);

    // Legacy: record aggregated latencies (kept for backward compat)
    void record_chunk_latency(
        std::chrono::microseconds latency);

    void record_getcommand_latency(
        std::chrono::microseconds latency);

    // New: record CompleteCommand latency separately
    void record_completecommand_latency(
        std::chrono::microseconds latency);

    // Record sink write latency separately
    void record_sink_latency(
        std::chrono::microseconds latency);

    void record_latency_histogram(uint64_t us);

    void record_buffer_acquire(bool success);
    void record_buffer_release();

    // Access to full ChunkTiming vector for post-processing / JSON export
    const std::vector<ChunkTiming>& timings() const { return timings_; }

    // Public timer accessor for hot-path timing in VdiClient
    uint64_t now_us() const { return timer_->now_us(); }

    // Total bytes transferred (for stall detection and diagnostics)
    uint64_t total_bytes() const { return total_bytes_.load(); }

    // Public accessors for hot-path retrieval
    const std::vector<uint64_t>& sink_latencies() const { return sink_latencies_; }

    void print_summary() const;

    // Machine-readable JSON output for benchmark scripts.
    // Returns a JSON string with all metrics (throughput, latency, CPU, etc.)
    std::string to_json() const;

private:
    static uint64_t percentile(
        const std::vector<uint64_t>& values,
        double p);

    // The timer used for this session
    std::unique_ptr<Timer> timer_;

    // Enable per-chunk debug logging
    bool logging_enabled_;

    // Aggregate byte/chunk totals
    std::atomic<uint64_t> total_bytes_;
    std::atomic<uint64_t> chunk_count_;

    // Chunk size tracking (min / max / running sum for average)
    std::atomic<uint64_t> min_chunk_size_;
    std::atomic<uint64_t> max_chunk_size_;
    std::atomic<uint64_t> sum_chunk_sizes_;

    // Latency histogram buckets (chunk processing, microseconds)
    std::atomic<uint64_t> hist_under_100us_;   // <100 µs
    std::atomic<uint64_t> hist_100us_1ms_;     // 100 µs – 1 ms
    std::atomic<uint64_t> hist_1ms_10ms_;      // 1 ms – 10 ms
    std::atomic<uint64_t> hist_over_10ms_;     // >10 ms

    // Buffer pool accounting
    std::atomic<uint64_t> buffer_acquire_count_;
    std::atomic<uint64_t> buffer_release_count_;
    std::atomic<uint64_t> buffer_pool_hits_;
    std::atomic<uint64_t> buffer_pool_misses_;

    // Latency samples (microseconds).
    // NOTE: currently accessed from a single thread (command loop).
    // If ever used from multiple threads, a mutex must be added.
    std::vector<uint64_t> getcommand_latencies_;
    std::vector<uint64_t> chunk_latencies_;
    std::vector<uint64_t> completecommand_latencies_;
    std::vector<uint64_t> sink_latencies_;

    // Full per-chunk timing breakdowns (the most valuable data)
    std::vector<ChunkTiming> timings_;

    std::chrono::steady_clock::time_point start_time_;
};