# VDI Wrapper Benchmark Results

## Environment

| Item | Value |
|---|---|
| VM type | Azure D2s_v5 |
| vCPUs | 2 |
| RAM | 8 GB |
| OS | Windows Server 2022 |
| SQL Server | SQL Server 2022 Developer |
| Storage | Standard SSD (managed disk) |
| Database | tempdb |
| Database size | 5 GB |
| Backup type | Full |
| Device count | 1 |
| C++ build | MSVC x64, `/O2`, C++17 |
| Python | 3.13.3 (embedded), ctypes COM |
| VDI device name | `VDI_Device_<pid>` (C++), `VDI_PyBench_<pid>` (Python) |

## Workload

| Item | Value |
|---|---|
| Sink | NullSink |
| Logging | OFF |
| Buffer pool | 64 buffers × 64 KB |

## Metrics Collected

- **Throughput**: MB/s (total_bytes / 1024² / elapsed_seconds)
- **Chunk size**: min / avg / max (bytes + KB)
- **GetCommand latency**: P50, P95, P99 (µs)
- **Chunk processing latency**: P50, P95, P99 (µs)
- **Latency histogram**: <100µs, 100µs–1ms, 1ms–10ms, >10ms (count + %)
- **CPU utilization**: user ms, kernel ms, total CPU%
- **Buffer pool**: acquires, releases, hits, misses, reuse rate%

---

## Results

### C++ vdi_wrapper (logging OFF)

| Metric | Value |
|---|---|
| Throughput | XX.X MB/s |
| Chunks processed | |
| Min chunk size | bytes ( KB) |
| Avg chunk size | bytes ( KB) |
| Max chunk size | bytes ( KB) |

**GetCommand Latency:**

| P50 | P95 | P99 |
|---|---|---|
| us | us | us |

**Chunk Processing Latency:**

| P50 | P95 | P99 |
|---|---|---|
| us | us | us |

**Latency Histogram:**

| Bucket | Count | % |
|---|---|---|
| <100 us | | % |
| 100 us–1 ms | | % |
| 1 ms–10 ms | | % |
| >10 ms | | % |

**CPU:**

| User | Kernel | Total |
|---|---|---|
| ms | ms | % |

**Buffer Pool:**

| Acquires | Releases | Hits | Misses | Reuse rate |
|---|---|---|---|---|
| | | | | % |

---

### Python VDI client (logging OFF)

| Metric | Value |
|---|---|
| Throughput | XX.X MB/s |
| Chunks processed | |
| Min chunk size | bytes ( KB) |
| Avg chunk size | bytes ( KB) |
| Max chunk size | bytes ( KB) |

**GetCommand Latency:**

| P50 | P95 | P99 |
|---|---|---|
| us | us | us |

**Chunk Processing Latency:**

| P50 | P95 | P99 |
|---|---|---|
| us | us | us |

**Latency Histogram:**

| Bucket | Count | % |
|---|---|---|
| <100 us | | % |
| 100 us–1 ms | | % |
| 1 ms–10 ms | | % |
| >10 ms | | % |

**CPU:**

| User | Kernel | Total |
|---|---|---|
| ms | ms | % |

**Buffer Pool:**

| Acquires | Releases | Hits | Misses | Reuse rate |
|---|---|---|---|---|
| | | | | % |

---

### Logging Impact (C++ with logging ON vs OFF)

| Metric | OFF | ON | Delta |
|---|---|---|---|
| Throughput | MB/s | MB/s | ±% |
| P50 chunk latency | us | us | ±% |
| P95 chunk latency | us | us | ±% |
| P99 chunk latency | us | us | ±% |
| CPU utilization | % | % | ±pp |
| Chunks processed | | | |

---

## Analysis

### 1. Syscall Amplification

With N = **XXXX** chunks, each chunk requires:

```
GetCommand      → 1 RPC transition (SQL Server → VDI client)
CompleteCommand → 1 RPC transition (VDI client → SQL Server)
```

**Total protocol crossings: 2 × N = XXXX**

For **25,000 chunks → 50,000 FIFO RPC transitions**.

**Python penalty**: Each crossing adds:
- FFI dispatch barrier (ctypes marshaling the call)
- Interpreter dispatch (Python bytecode loop)
- Object wrapping (creating/managing Python objects for each command)

**C++ advantage**: Direct native vtable call — no intermediate layers.

### 2. Tail Latency (Most Important)

Backup systems are **streaming systems**. Large latency spikes stall the pipeline and increase end-to-end time.

| Metric | C++ | Python | C++ advantage |
|---|---|---|---|
| P50 | us | us | × |
| P95 | us | us | × |
| P99 | us | us | × |

**Key insight**: The gap between P50 and P99 reveals runtime scheduling stability.
- Smaller gap → fewer outliers → more predictable backup window
- C++ expected to show tighter tail distribution due to:
  - No garbage collection pauses
  - No interpreter preemption
  - Direct memory access (no ctypes indirection)

### 3. Chunk Behavior

Observed chunk sizes: **XXX, XXX, XXX, XXX** (fill from min/avg/max results).

- **Mixed metadata/data blocks**: SQL Server emits both small metadata blocks (often 512–4096 bytes) and large data blocks (up to 65536 bytes = 64 KB)
- **Stream is not uniform**: Small chunks (<4 KB) amplify per-chunk overhead disproportionately
- **Metadata-to-data ratio**: ratio tells us about backup stream structure

**Why this matters**: 
- Small chunks → high overhead per byte transferred
- Large chunks → efficient bulk transfer
- If min ≪ avg, many metadata chunks exist in the stream

### 4. Buffer Reuse Effects

**C++**: BufferPool pre-allocates 64 buffers × 64 KB in the constructor. During streaming, `acquire()`/`release()` are purely pointer-queue operations — **zero transient allocations**. Stable memory footprint throughout.

**Python**: `bytearray` objects are allocated at init time for the pool simulation, but every chunk operation in pure Python creates temporary objects (int wrappers, tuple returns). The pool simulation uses list `pop(0)` which has O(n) overhead.

**Reuse rate**: Both should show 100% since pool never exhausts (64 buffers, single-threaded sequential acquire/release).

**Result**: C++ memory footprint stable. Python sees more GC pressure from transient objects.

### 5. Runtime Scheduling Differences

| | C++ | Python |
|---|---|---|
| User CPU | ms | ms |
| Kernel CPU | ms | ms |
| Total CPU% | % | % |
| Wall time | s | s |

**Interpretation**:
- **High kernel %** → syscall/context-switch bound (VDI RPC overhead dominates)
- **High user %** → computation bound
- C++ expected to show **higher total CPU%** (more work done per unit wall time)
- Python expected to show **higher kernel %** relative to total (more OS transitions from FFI)

---

## Observations

<!-- Fill after benchmark runs:

- Throughput difference (if any) between C++ and Python
- Tail latency disparity (P99 gap)
- Whether logging significantly impacts performance
- Any anomalies (e.g., >10ms spikes)
- CPU utilization patterns
-->

---

## Pipeline Documentation

```
SQL Server
    │
    ▼
VDI command dispatch ────── GetCommand() ──► RPC barrier ◄── SQL Server FIFO
    │
    ▼
VDC_Command received ────── buffer pointer + size + commandCode
    │
    ├── VDC_Write:   process_write() ──► record_chunk_size() → add_bytes() → sink write → latency record
    ├── VDC_Flush:   sink_->flush()
    ├── VDC_Read:    (not used in backup, only restore)
    ├── VDC_ClearError: acknowledge
    └── VDC_Close:   shutdown
    │
    ▼
CompleteCommand() ────────── RPC barrier ──► SQL Server
    │
    ▼
Next iteration
```

**Where latency accumulates**:
1. **GetCommand**: Blocks waiting for SQL Server to issue next command. Time depends on SQL Server's internal scheduling.
2. **sink_->write()**: I/O subsystem (buffered write via FileSink).
3. **CompleteCommand**: RPC response back to SQL Server.

**Where allocations happen**:
- VDI provides `cmd->buffer` — no allocation per chunk
- Python: ctypes creates `VDC_Command` object, accessing `.contents` allocates
- C++: zero allocations in the hot path

**Where runtime overhead appears**:
- Python: FFI dispatch for every GetCommand/CompleteCommand call
- C++: direct vtable dispatch