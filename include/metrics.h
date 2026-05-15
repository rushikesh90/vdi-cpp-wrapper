#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

class Metrics {
public:
    Metrics();

    void add_bytes(size_t size);

    void record_chunk_latency(
        std::chrono::microseconds latency);

    void record_getcommand_latency(
        std::chrono::microseconds latency);

    void print_summary() const;

private:
    static uint64_t percentile(
        const std::vector<uint64_t>& values,
        double p);

    std::atomic<uint64_t> total_bytes_;
    std::atomic<uint64_t> chunk_count_;

    // Latency samples (microseconds).
    // NOTE: currently accessed from a single thread (command loop).
    // If ever used from multiple threads, a mutex must be added.
    std::vector<uint64_t> getcommand_latencies_;
    std::vector<uint64_t> chunk_latencies_;

    std::chrono::steady_clock::time_point start_time_;
};