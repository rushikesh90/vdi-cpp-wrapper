#pragma once

#include <atomic>
#include <chrono>

class Metrics {
public:
    Metrics();

    void add_bytes(size_t size);

    void record_chunk_latency(
        std::chrono::microseconds latency);

    void print_summary() const;

private:
    std::atomic<uint64_t> total_bytes_;
    std::atomic<uint64_t> chunk_count_;

    std::chrono::steady_clock::time_point start_time_;
};