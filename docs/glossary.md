# Glossary

> **Terms and concepts used in the VDI C++ Wrapper project.**

---

### VDI (Virtual Device Interface)

A COM-based protocol that allows SQL Server to stream backup data directly to a
consumer application, bypassing the file system. The consumer receives commands
(Write, Read, Flush, etc.) and responds by processing data buffers.

VDI is the foundation of SQL Server's `BACKUP TO VIRTUAL_DEVICE` syntax.

---

### COM (Component Object Model)

Microsoft's binary interface standard for inter-process communication. VDI is
implemented as a set of COM interfaces (`IClientVirtualDeviceSet2`,
`IClientVirtualDevice`). Correct use requires:

- `CoInitializeEx` / `CoUninitialize` for apartment management
- Reference counting via `AddRef` / `Release`
- Correct vtable layout when inheriting interfaces

---

### VSS (Volume Shadow Copy Service)

A Windows framework for creating consistent point-in-time snapshots of volumes.
SQL Server integrates with VSS for backup, but this project does not use VSS —
it operates at the raw VDI level.

---

### Tail Latency

The worst-case latency experienced by a small fraction of requests. Commonly
reported as the P95 (95th percentile) or P99 (99th percentile) latency.

In streaming systems, tail latency matters because a single slow chunk can stall
the entire pipeline.

---

### Throughput

The rate at which data is transferred, typically measured in MB/s (megabytes per
second). In this project, throughput is calculated as `total_bytes / elapsed_seconds`.

Throughput is distinct from bandwidth: bandwidth is the theoretical maximum,
throughput is what was actually achieved.

---

### Chunk

A single unit of data delivered by VDI's `GetCommand` and processed by the
command loop. Each chunk has a size (typically 64 KB to 1 MB), a buffer pointer,
a stream position, and a command code (Write, Read, Flush, etc.).

---

### I/O Completion

The act of signalling to SQL Server that a command has been fully processed, via
`CompleteCommand`. SQL Server uses this for flow control — it will not send the
next command until the previous one is completed.

---

### FIFO Protocol

A protocol where commands are processed in the exact order they are received
(First In, First Out). VDI is a FIFO protocol: commands for a given device are
delivered sequentially and must be completed in order.

---

### Session State Machine

A finite state machine that tracks the lifecycle of a VDI session:

```
INIT → CONNECTED → STREAMING → FLUSHING → CLOSED
  ↓        ↓          ↓                     ↑
FAILED ←───┴──────────┴─────────────────────┘
```

Each state guards against invalid transitions (e.g., calling `process_commands`
before `connect`).

---

### Fault Injection

A testing technique where failures are deliberately introduced to verify that
error handling code works correctly. This project includes a compile-time gated
`FaultInjector` that can simulate failures after a configurable number of chunks.

---

### Latency Histogram

A distribution of latency measurements grouped into buckets (e.g., <100 µs,
100 µs–1 ms, 1–10 ms, >10 ms). While informative, full vector storage in
`ChunkTiming` is preferred for deeper analysis.

---

### GetCommand Latency

The time spent waiting for `GetCommand` to return from SQL Server. This is a
blocking call — high latency here means SQL Server is not sending data fast
enough (or has paused).

---

### Sink Write Latency

The time spent writing a chunk to the output destination (null device, file,
or custom sink). This is the I/O portion of the pipeline and is the most
variable stage.

---

### CompleteCommand Latency

The time spent signalling completion back to SQL Server. This is typically very
low (microseconds) but can spike under heavy load.

---

### Dispatch Overhead

The time spent in command dispatch logic (the `switch` statement that decides
what to do with each command). This is the control-plane portion of the pipeline
and should be negligible.