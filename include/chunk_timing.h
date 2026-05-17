#pragma once

// ---------------------------------------------------------------------------
// Per-chunk timing breakdown for the VDI backup pipeline.
//
// Each chunk processed by the command loop produces one ChunkTiming entry
// recording duration of each stage separately:
//
//   GetCommand() → dispatch → sink.write() → CompleteCommand()
//
// Full vectors of ChunkTiming enable:
//   - Per-stage latency distributions (P50/P95/P99)
//   - Outlier / scheduler-jitter detection
//   - Chunk-size correlation
//   - Burst analysis via timestamp_us
// ---------------------------------------------------------------------------

#include <cstdint>

struct ChunkTiming {
    uint64_t size;            // Chunk size in bytes (from cmd->size)
    uint64_t getcommand_us;   // Time spent in GetCommand()
    uint64_t dispatch_us;     // Command dispatch overhead (switch + decision)
    uint64_t sink_us;         // Time spent in sink->write()
    uint64_t complete_us;     // Time spent in CompleteCommand()
    uint64_t total_us;        // Sum of all stages (wall-clock for this chunk)
    uint64_t timestamp_us;    // Microseconds since session start (at chunk begin)
};