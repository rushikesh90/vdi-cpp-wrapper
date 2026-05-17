# VDI C++ Wrapper

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Native C++ implementation of SQL Server **Virtual Device Interface (VDI)** streaming
used to study backup pipeline latency, runtime overhead, and buffer management
behavior compared to Python-based orchestration.

Instrumentation spans the full lifecycle:

    GetCommand() → command dispatch → sink.write() → CompleteCommand()

Each stage is timed independently, with per-chunk breakdowns (P50/P95/P99),
kernel/user CPU decomposition, and logging overhead measured scientifically
(3 build configurations: OFF / INFO / DEBUG).

---

## Why VDI Is Difficult

SQL Server's VDI is a **COM-based FIFO protocol** that requires:

- Correct COM apartment initialisation (`CoInitializeEx`)
- Exact vtable layout for `IClientVirtualDeviceSet2` (inherits
  `IClientVirtualDeviceSet`, not `IUnknown` directly)
- The right three-argument `CreateEx` signature (not the two-argument one
  found in many blog posts)
- All 11 fields of `VDConfig` filled in (incomplete structs return
  `VD_E_INVALID`)
- A specific call order: `CreateEx` → `GetConfiguration` (blocks for SQL) →
  `OpenDevice` → command loop
- Proper `CompleteCommand(4 args)` calls in the command loop — wrong
  signature causes stack corruption

This repository captures those lessons in a clean, minimal implementation.

---

## Architecture

```
SQL Server
    │
    ▼
┌─────────────────────┐
│  VDI COM Interface  │  IClientVirtualDeviceSet2
│  (sqlvdi.h)         │
└─────────┬───────────┘
          │ GetCommand / CompleteCommand
          ▼
┌─────────────────────┐
│  Command Loop       │  VdiClient::process_commands()
│  (vdi_client.cpp)   │  ┌──────────────┐
│                     │  │ GetCommand    │  Stage 1
│                     │  │ Dispatch      │  Stage 2
│                     │  │ sink.write()  │  Stage 3
│                     │  │ CompleteCmd   │  Stage 4
│                     │  └──────────────┘
└─────────┬───────────┘
          │ per-chunk ChunkTiming
          ▼
┌─────────────────────┐
│  Metrics Layer      │  Throughput, per-stage P50/P95/P99, CPU%
│  (metrics.h/.cpp)   │  Full vector storage for distribution analysis
└─────────┬───────────┘
          ▼
┌─────────────────────┐
│  Sink               │  NullSink | FileSink | Custom
│  (sink.h)           │
└─────────────────────┘
```

---

## Per-Stage Timing Breakdown

Each chunk's processing lifecycle is decomposed into four independently
timed stages:

| Stage | Timing Field | What It Measures |
|-------|-------------|------------------|
| 1 | `getcommand_us` | `GetCommand()` COM call — blocks waiting for SQL Server |
| 2 | `dispatch_us` | Command dispatch overhead (switch + decision logic) |
| 3 | `sink_us` | `sink->write()` — the actual I/O to disk/null |
| 4 | `complete_us` | `CompleteCommand()` — SQL sync / flow control |

The `ChunkTiming` struct stores all seven fields:

```cpp
struct ChunkTiming {
    uint64_t size;            // Chunk size in bytes
    uint64_t getcommand_us;   // GetCommand() duration
    uint64_t dispatch_us;     // Dispatch overhead
    uint64_t sink_us;          // Sink write duration
    uint64_t complete_us;     // CompleteCommand() duration
    uint64_t total_us;        // Sum of all stages
    uint64_t timestamp_us;    // Microseconds since session start
};
```

Full vectors are stored (not running sums), enabling:
- Per-stage latency distributions (P50/P95/P99)
- Outlier / scheduler-jitter detection
- Chunk-size vs latency correlation
- Burst/pause analysis via timestamps

---

## Logging Overhead Measurement

Three build configurations allow scientific measurement of logging overhead:

| Level | `#define LOGGING_LEVEL` | Binary | What's Compiled |
|-------|------------------------|--------|-----------------|
| OFF | 0 | `vdi_wrapper_no_logging.exe` | Only `LOG_ERROR` — zero debug/info overhead |
| INFO | 1 | `vdi_wrapper_info.exe` | Session lifecycle events, no per-chunk tracing |
| DEBUG | 2 | `vdi_wrapper_debug.exe` | Full per-chunk `LOG_DEBUG` + `TRACE_EVENT` RAII |

Compile-time elimination guarantees the OFF binary has **zero** debug-path
overhead in the hot path — no branch prediction changes, no formatting,
no iostream/mutex state touched.

### Building each configuration

```powershell
# OFF  (no logging)
cl /DLOGGING_LEVEL=0 src\*.cpp /EHsc /std:c++17 /link ole32.lib

# INFO (default, or omit)
cl /DLOGGING_LEVEL=1 src\*.cpp /EHsc /std:c++17 /link ole32.lib

# DEBUG (trace events + debug logging)
cl /DLOGGING_LEVEL=2 src\*.cpp /EHsc /std:c++17 /link ole32.lib
```

### TRACE_EVENT RAII

In DEBUG builds, scope-based trace events provide lightweight instrumentation:

```cpp
{  TRACE_EVENT("SinkWrite");
   sink->write(data, size);  }
// prints: [TRACE] SinkWrite tid=1234 52 us
```

Each trace records thread ID (`GetCurrentThreadId()`) and elapsed microseconds.

---

## Timer Abstraction

Timing uses a virtual `Timer` interface with two backends:

| Backend | Class | Precision | Portability |
|---------|-------|-----------|------------|
| `std::chrono` | `ChronoTimer` | Microseconds (typical) | Cross-platform (default) |
| QueryPerformanceCounter | `QPCTimer` | Sub-microsecond | Windows only (optional) |

Default is `ChronoTimer`. Pass `TimerMode::QPC` to the `VdiClient` constructor
to use the high-resolution Windows counter.

---

## Benchmark Methodology

The project includes reproducible benchmark scripts that compare C++ (native COM)
against Python (ctypes COM) implementations. See:

- [`docs/benchmark-methodology.md`](docs/benchmark-methodology.md) — Workload,
  environment, protocol, and reproducibility checklist
- [`benchmarks/results/results.md`](benchmarks/results/results.md) — Latest
  benchmark results with full environment documentation
- [`docs/runtime-analysis.md`](docs/runtime-analysis.md) — Runtime behavior,
  chunk distribution, scheduler effects, logging impact, tail latency,
  allocation stability

### Quick benchmark run

```powershell
# C++ benchmark (builds, runs, outputs JSON)
.\scripts\run_cpp_benchmark.ps1

# Python benchmark (same pattern)
.\scripts\run_python_benchmark.ps1
```

Both scripts:
1. Build the client (C++ or Python)
2. Prompt for the T-SQL `BACKUP DATABASE ... TO VIRTUAL_DEVICE` command
3. Collect metrics and output machine-readable JSON

---

## Results

| Metric | C++ | Python |
|--------|-----|--------|
| Throughput | XX.X MB/s | XX.X MB/s |
| P50 GetCommand | XX µs | XX µs |
| P50 CompleteCommand | XX µs | XX µs |
| P50 sink write | XX µs | XX µs |
| P95 sink write | XX µs | XX µs |
| P99 sink write | XX µs | XX µs |
| CPU utilization | XX% | XX% |

*(Values pending first benchmark run — see
[results](benchmarks/results/results.md) for the latest numbers and
[runtime-analysis.md](docs/runtime-analysis.md) for detailed observations.)*

---

## API Surface

The public API is intentionally minimal:

```cpp
class VdiClient {
public:
    // Connect to VDI device set
    bool connect(const std::wstring& device_name, int device_count);

    // Run the blocking command loop (backup data flow)
    void process_commands();
};
```

That's it. Protocol ugliness (`VDC_Command`, `VDConfig`, COM interface pointers)
stays in `sqlvdi.h` — never leaks into caller code.

---

## Build Instructions

### Prerequisites

| Requirement | Version |
|---|---|
| Windows | Windows Server 2022+ |
| SQL Server | SQL Server 2019+ (Developer Edition is free) |
| Visual Studio | 2022 with "Desktop development with C++" |
| VDI SDK | Included with SQL Server (`C:\Program Files\Microsoft SQL Server\...\COM\`) |
| CMake | 3.16+ |

### Build

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The binary `vdi_wrapper.exe` will be at `build\bin\Release\vdi_wrapper.exe`.

### Build from source (MSVC CLI)

```powershell
# Set up environment (paths will differ on your machine)
$env:Path = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;" + $env:Path
$env:INCLUDE = "...\include;...\Windows Kits\10\Include\10.0.26100.0\ucrt;..."
$env:LIB = "...\lib\x64;..."

# OFF logging build
cl /DLOGGING_LEVEL=0 src\sqlvdi_guids.cpp /c /EHsc /std:c++17 /Fosqlvdi_guids.obj
cl /DLOGGING_LEVEL=0 src\vdi_client.cpp /c /EHsc /std:c++17 /Fovdi_client.obj
cl /DLOGGING_LEVEL=0 src\metrics.cpp /c /EHsc /std:c++17 /Fometrics.obj
cl /DLOGGING_LEVEL=0 src\main.cpp /c /EHsc /std:c++17 /Fomain.obj
cl /DLOGGING_LEVEL=0 src\buffer_pool.cpp /c /EHsc /std:c++17 /Fobuffer_pool.obj
cl /DLOGGING_LEVEL=0 src\file_sink.cpp /c /EHsc /std:c++17 /Fofile_sink.obj

link *.obj /OUT:vdi_wrapper_no_logging.exe ole32.lib /SUBSYSTEM:CONSOLE
```

---

## VDI Protocol Flow

See [`docs/vdi-flow.md`](docs/vdi-flow.md) for a complete description of the
VDI lifecycle, command loop, and data flow.

---

## Failure Handling

The implementation includes defense-in-depth for production use:

- **Graceful VDI session teardown**: `VD_E_CLOSE` triggers flush → device
  release → COM uninit in ordered sequence.
- **Explicit session state machine**: `SessionState` enum (`INIT`, `CONNECTED`,
  `STREAMING`, `FLUSHING`, `CLOSED`, `FAILED`) prevents invalid transitions,
  double-close bugs, and provides clear error context.
- **Timeout-aware command processing**: `GetCommand` uses a configurable timeout
  (default 30s) instead of `INFINITE`, preventing protocol stalls from hanging
  the process indefinitely.
- **Stall detection**: Logs warnings when no byte progress is made within a
  60-second window, helping diagnose hung SQL Server connections or blocked
  sinks.
- **Sink failure propagation**: Write failures set the session to `FAILED`
  state and abort the command loop, preventing silent data loss. Sink health
  is checked via `is_open()` before each write.
- **Defensive protocol validation**: Incoming `VDC_Write` commands are validated
  for null buffers, zero/insane sizes before processing.
- **Idempotent cleanup**: `close()` can be called multiple times safely; the
  destructor auto-cleans if `close()` was never called explicitly.
- **HRESULT translation**: All VDI protocol errors are logged with
  human-readable strings (`hresult_to_string`) in addition to hex codes.
- **Resource accounting**: Active buffer counts and high-water marks are tracked
  and logged at session end to detect leaks.
- **Human-readable error codes**: `hresult_to_string()` maps all VDI protocol
  error codes and common COM errors to descriptive text.

See [`docs/operational-behavior.md`](docs/operational-behavior.md) for detailed
documentation of the session state machine, shutdown sequencing, timeout
configuration, cleanup guarantees, retry policy, and fault injection hooks.

---

## Limitations

While significantly hardened, the following limitations remain:

- **Single-threaded**: Command loop runs synchronously on one thread.
- **No async sink**: All I/O blocks the command loop.
- **No restore pipeline**: Only `VDC_Write` is instrumented; `VDC_Read` is not
  tested (restore scenario).
- **No VSS integration**: Snapshot-based backups are not supported.
- **No compression or encryption**: Raw data only.
- **No cloud storage**: Sink writes to local file or null.
- **No automated retry**: Retry decisions are left to the operator (see
  [retry policy](docs/operational-behavior.md#6-retry-policy)).

If you need any of these, this repository serves as a reference implementation
to build upon, not a turnkey backup solution.

---

## License

MIT — see [LICENSE](LICENSE).
