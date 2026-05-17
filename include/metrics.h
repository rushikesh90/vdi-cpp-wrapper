#pragma once

// ---------------------------------------------------------------------------
// Metrics collection for VDI backup pipeline instrumentation.
//
// Tracks:
//   - Total bytes and chunk count (atomic for stall detection)
//   - Per-chunk timing breakdowns via ChunkTiming vector
//   - Per-stage latency percentiles (P50/P95/P99) from ChunkTiming data
//   - CPU utilisation via GetProcessTimes (Windows-only)
//   - Buffer pool high-water mark (caller-reported)
// ---------------------------------------------------------------------------

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "chunk_timing.h"
#include "timer.h"

class Metrics {
public:
    explicit Metrics(std::unique_ptr<Timer> timer = std::make_unique<ChronoTimer>(),
                     bool enable_logging = false);
    ~Metrics() = default;

    // No copy or move
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    Metrics(Metrics&&) = delete;
    Metrics& operator=(Metrics&&) = delete;

    // ── Byte and chunk accounting ─────────────────────────────────────────
    void add_bytes(size_t size);
    void record_chunk_size(size_t size);

    // ── Per-chunk timing ──────────────────────────────────────────────────
    // Records a full ChunkTiming struct. This is the primary recording path;
    // all per-stage latencies and distributions are derived from the stored
    // ChunkTiming vector.
    void record_chunk_timing(const ChunkTiming& timing);

    // ── Buffer pool instrumentation ───────────────────────────────────────
    void record_buffer_acquire(bool success);
    void record_buffer_release();

    // ── Timing accessor for hot-path use ──────────────────────────────────
    uint64_t now_us() const { return timer_->now_us(); }

    // ── Accessors for diagnostics and JSON export ─────────────────────────
    uint64_t total_bytes() const { return total_bytes_.load(); }
    uint64_t chunk_count() const { return chunk_count_.load(); }
    const std::vector<ChunkTiming>& timings() const { return timings_; }

    // ── Output ────────────────────────────────────────────────────────────
    void print_summary() const;

    // Machine-readable JSON output for benchmark scripts.
    // Includes throughput, per-stage latency distributions, CPU%, and
    // environment metadata.
    std::string to_json() const;

private:
    // Compute the p-th percentile from a sorted vector of values.
    static uint64_t percentile(
        const std::vector<uint64_t>& values,
        double p);

    // Extract a stage vector from timings_ for percentile computation.
    std::vector<uint64_t> stage_values(
        uint64_t ChunkTiming::*field) const;

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

    // Buffer pool accounting
    std::atomic<uint64_t> buffer_acquire_count_;
    std::atomic<uint64_t> buffer_release_count_;
    std::atomic<uint64_t> buffer_pool_hits_;
    std::atomic<uint64_t> buffer_pool_misses_;

    // Full per-chunk timing breakdowns (the most valuable data).
    // All per-stage latencies (P50/P95/P99) are derived from this vector.
    std::vector<ChunkTiming> timings_;

    std::chrono::steady_clock::time_point start_time_;
};