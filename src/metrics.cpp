#include "metrics.h"
#include <iostream>

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

    // placeholder for P99 later
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

    std::cout << "\n=== Metrics ===\n";
    std::cout << "Bytes: "
              << total_bytes_ << "\n";

    std::cout << "Chunks: "
              << chunk_count_ << "\n";

    std::cout << "Throughput MB/s: "
              << throughput << "\n";
}