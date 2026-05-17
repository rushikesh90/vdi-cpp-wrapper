# Design Decisions

> **Rationale for key architectural choices in the VDI C++ Wrapper.**

---

## Why Single-Threaded Initially

The VDI protocol is a deterministic FIFO channel. SQL Server delivers commands
one at a time per device, and each `CompleteCommand` acts as a synchronisation
point. Adding threads would introduce:

- Complexity without proven throughput benefit (the wire is the bottleneck)
- Risk of out-of-order `CompleteCommand` calls
- Additional measurement noise in latency distributions

If multi-stream parallelism is needed later, the clean separation between
`VdiClient` (session lifecycle) and the command loop (per-stream processing)
makes this possible without refactoring the public API.

---

## Why Buffer Reuse Matters

Each VDI chunk arrives with a buffer allocated by SQL Server's VDI provider.
Copying this data incurs:

- Memory allocation overhead in the hot path
- Cache pollution from touching additional memory pages
- Increased page fault jitter

The `BufferPool` pre-allocates 64 buffers of 64 KB each at construction time,
eliminating allocation from the per-chunk path. While the current implementation
uses this pool for instrumentation simulation only, the design is ready for
zero-copy buffer handoff when a direct-I/O sink is needed.

---

## Why NullSink Benchmark Exists

The `NullSink` discards all data without touching disk. This isolates protocol
overhead from I/O overhead:

| Component | NullSink | FileSink |
|-----------|----------|----------|
| `GetCommand` latency | ✓ | ✓ |
| `CompleteCommand` latency | ✓ | ✓ |
| Dispatch overhead | ✓ | ✓ |
| Sink write latency | Removed | Included |
| Disk I/O latency | Zero | Included |
| Jitter from OS page cache | None | Included |

Benchmark results should always specify which sink was used. Comparing NullSink
to FileSink numbers directly is misleading.

---

## Why Tail Latency (P95/P99) Is Emphasized

Backup pipelines are latency-sensitive streaming systems. A single stalled chunk
can delay the entire backup window:

- **P50** tells you the typical case
- **P95** tells you about scheduler interference or resource contention
- **P99** tells you about outliers that can cause SLA violations

The `ChunkTiming` vector stores every sample, enabling distribution analysis
beyond simple percentiles — including burst detection, autocorrelation, and
comparison across runs.

---

## Why ChunkTiming Stores Full Vectors

Running sums (min/max/avg) lose distribution information. Full vectors enable:

- **Percentile analysis**: P50/P95/P99 from actual data, not assumptions
- **Outlier detection**: Identifying chunks that took 10× longer than the median
- **Chunk-size correlation**: Are larger chunks slower? The data can tell you.
- **Burst analysis**: Timestamps reveal idle periods and burst patterns
- **Cross-run comparison**: Compare latency CDFs across configurations

The storage cost is proportional to chunk count (typical: 10,000–100,000 chunks
for a 5 GB backup at 512 KB average). This is cheap relative to the insight
gained.

---

## Why Compile-Time Logging

Three build configurations (OFF / INFO / DEBUG) use compile-time `#if`
elimination rather than runtime flags:

- **Zero overhead in release**: When `LOGGING_LEVEL=0`, no logging branches or
  format strings exist in the binary. No branch prediction pollution.
- **Measurable overhead**: The difference between OFF and INFO is measurable
  (a few percent). The difference between INFO and DEBUG is significant (10–20%).
- **Scientific integrity**: Each configuration is a genuine artifact, not a
  runtime toggle that may leave dead code or branch predictors in a different
  state.

---

## Why Three-Argument CreateEx

The VDI specification defines `CreateEx` with three parameters:

```cpp
HRESULT CreateEx(
    LPCWSTR lpInstanceName,   // nullptr for default instance
    LPCWSTR lpName,           // the VDI device name
    VDConfig *pCfg);          // configuration
```

Many blog posts and sample code use a two-argument version:

```cpp
HRESULT CreateEx(LPCWSTR lpName, VDConfig *pCfg);  // WRONG
```

This causes `VD_E_NOTSUPPORTED` (0x80770009) because the actual vtable entry
expects three arguments. The stack cleanup differs, and the COM infrastructure
detects the mismatch.

---

## Why IClientVirtualDeviceSet2 Inherits IClientVirtualDeviceSet

The COM interface hierarchy is:

```
IUnknown
  ↑
IClientVirtualDeviceSet   (8 methods: QueryInterface/AddRef/Release + 5 VDI methods)
  ↑
IClientVirtualDeviceSet2  (2 additional methods: CreateEx + OpenInSecondaryEx)
```

Defining `IClientVirtualDeviceSet2 : public IUnknown` (instead of
`IClientVirtualDeviceSet`) puts `CreateEx` at vtable slot 3 instead of slot 11.
Calling through the wrong slot reads garbage from the stack, causing:

- "Run-Time Check Failure: ESP not properly saved" crashes in debug builds
- Silent data corruption in release builds

This was the root cause of the most expensive debugging session in this
project's history.

---

## Why Protocol Types Are Not in the Public API

Early versions of this library leaked `VDC_Command*`, `VDConfig`, and
`IClientVirtualDeviceSet2*` into the public `vdi_client.h` header. This meant:

- Users had to include `sqlvdi.h` (with its COM interface definitions)
- Changing protocol internals broke the public API
- The library felt like a thin wrapper, not an abstraction

The refactored API exposes only:

```cpp
class VdiClient { ... };    // session lifecycle
class Sink { ... };         // output abstraction
class Metrics { ... };      // instrumentation results
class BufferPool { ... };   // buffer management
```

Protocol handling is internal to `src/vdi_protocol.h` and `src/vdi_protocol.cpp`.
The public header includes only forward declarations and opaque `void*` handles.

---

## Why No Automated Retry

The VDI protocol is a deterministic FIFO channel. Retrying a failed
`CompleteCommand` or write risks data corruption because the protocol state
machine may be in an undefined position.

Only timeouts and configuration delays are candidates for retry — and even
those are left to the operator, not automated. See
[docs/operational-behavior.md#6-retry-policy](operational-behavior.md#6-retry-policy)
for the full rationale.

---

## Why the Repo Is Versioned v0.1.0

Semantic versioning signals:

- **0.x.y**: The API is still evolving. Breaking changes may occur without
  major version bumps.
- **x.1.x**: Backwards-compatible functionality (new features, benchmarks).
- **x.x.0**: Initial public release with documented API surface.

The version is defined in `include/version.h` and embedded in benchmark JSON
output for traceability.