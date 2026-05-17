# Runtime Analysis

> **Status**: Placeholder — populated after first benchmark run.
>
> This document captures empirical observations of the VDI backup pipeline's
> runtime behavior. Each section is designed to be filled in from real
> benchmark data produced by `Metrics::print_summary()` and `Metrics::to_json()`.

---

## Runtime Behavior

Overall throughput, wall-clock time, and per-stage average breakdown.

| Metric | Value | Notes |
|--------|-------|-------|
| Database size | XX GB | Test workload |
| Total bytes transferred | XX GB | `Metrics::total_bytes_` |
| Chunk count | XX | `Metrics::chunk_count_` |
| Elapsed wall time | XX s | `Metrics::print_summary()` |
| Throughput | XX.X MB/s | `total_bytes / elapsed` |
| CPU utilization | XX% | kernel: XX ms, user: XX ms |
| Avg chunk size | XX KB | `total_bytes / chunk_count` |

### Per-Stage Average (from ChunkTiming vector)

| Stage | Avg (µs) | % of Total |
|-------|----------|------------|
| GetCommand | XX | XX% |
| Dispatch | XX | XX% |
| Sink write | XX | XX% |
| CompleteCommand | XX | XX% |
| **Total** | **XX** | **100%** |

---

## Chunk Distribution

Chunk size distribution across the backup session.

| Metric | Value |
|--------|-------|
| Min chunk size | XX bytes (XX KB) |
| Avg chunk size | XX bytes (XX KB) |
| Max chunk size | XX bytes (XX KB) |

### Histogram

| Bucket | Count | % |
|--------|-------|---|
| <100 µs | XX | XX% |
| 100 µs–1 ms | XX | XX% |
| 1 ms–10 ms | XX | XX% |
| >10 ms | XX | XX% |

### Observations

- *(Fill in: predominant chunk size, any bimodal distribution, correlation with stages)*

---

## Scheduler Effects

Thread descheduling events, long-tail pauses, and context-switch observations.

### TRACE_EVENT observations

When running with `LOGGING_LEVEL=2` (DEBUG), the `[TRACE]` output includes
`tid=<thread-id>`. Observations:

- Thread ID stability: (same TID throughout / migrated across cores?)
- Any pauses >10 ms not attributable to I/O?
- Distribution of long-tail events by stage

### Long-Tail Analysis

| Threshold | GetCommand | Dispatch | Sink | Complete |
|-----------|-----------|----------|------|----------|
| P99 | XX µs | XX µs | XX µs | XX µs |
| P99.9 | XX µs | XX µs | XX µs | XX µs |
| Max | XX µs | XX µs | XX µs | XX µs |

### Observations

- *(Fill in: any evidence of scheduler descheduling, thread migration, NUMA effects)*

---

## Logging Impact

Comparison across three logging modes.

| Mode | Throughput (MB/s) | P50 (µs) | P95 (µs) | P99 (µs) | CPU% |
|------|------------------|----------|----------|----------|------|
| OFF | XX.X | XX | XX | XX | XX% |
| INFO | XX.X | XX | XX | XX | XX% |
| DEBUG | XX.X | XX | XX | XX | XX% |

### Overhead Attribution

| Source | Estimated Cost | Notes |
|--------|---------------|-------|
| `fprintf` to stderr | XX µs per call | Visible in dispatch stage |
| Format string parsing | XX µs | Compile-time constant vs runtime |
| `TRACE_EVENT` RAII | XX µs | Constructor + destructor + file I/O |

### Observations

- *(Fill in: throughput regression from OFF→INFO, from INFO→DEBUG, whether
  logging overhead is uniform or bursty)*

---

## Tail Latency

Analysis of the 99.9th percentile and maximum observed latencies.

### Per-Stage P99.9

| Stage | P99.9 (µs) | Max (µs) | Context |
|-------|-----------|----------|---------|
| GetCommand | XX | XX | VDI COM call, SQL might batch |
| Dispatch | XX | XX | Switch overhead, usually negligible |
| Sink write | XX | XX | File I/O, buffered vs sync |
| CompleteCommand | XX | XX | SQL sync / flow control |

### Outlier Analysis

- Count of chunks with `total_us > 10 ms`: XX
- Count of chunks with `total_us > 100 ms`: XX
- Largest observed stall: XX µs at stage: XX

### Observations

- *(Fill in: what caused the tail? Sink I/O? CompleteCommand sync? Scheduler pause?)*

---

## Allocation Stability

Buffer pool behavior and allocation patterns over time.

| Metric | Value |
|--------|-------|
| Buffer pool size | 64 buffers |
| Buffer size | 64 KB |
| Total acquires | XX |
| Total releases | XX |
| Pool hits | XX |
| Pool misses | XX |
| Reuse rate | XX% |

### Observations

- *(Fill in: did misses correlate with burst periods? Was the pool size adequate?)*
- *(Fill in: any allocation/deallocation patterns visible in the trace?)*

---

## Methodology Notes

- All timestamps are relative to session start (`timestamp_us` in `ChunkTiming`).
- Timer used: `std::chrono` (default) or `QPC` if `TimerMode::QPC` was specified.
- CPU times from `GetProcessTimes()` (kernel + user, Windows only).
- Logging mode controlled by `#define LOGGING_LEVEL` at compile time.
- See [`docs/benchmark-methodology.md`](benchmark-methodology.md) for full
  workload and environment details.