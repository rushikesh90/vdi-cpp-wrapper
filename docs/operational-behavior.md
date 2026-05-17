# Operational Behavior

> **Documentation of failure handling, shutdown sequencing, timeout behavior,
> cleanup guarantees, and retry policy for the VDI C++ Wrapper.**

---

## Table of Contents

1. [Session State Machine](#1-session-state-machine)
2. [Shutdown Sequencing](#2-shutdown-sequencing)
3. [Timeout Handling](#3-timeout-handling)
4. [Cleanup Guarantees](#4-cleanup-guarantees)
5. [Stall Detection](#5-stall-detection)
6. [Retry Policy](#6-retry-policy)
7. [Sink Failure Propagation](#7-sink-failure-propagation)
8. [Fault Injection Testing](#8-fault-injection-testing)
9. [Resource Accounting](#9-resource-accounting)

---

## 1. Session State Machine

The `SessionState` enum tracks the lifecycle of a VDI session explicitly.

```
    ┌──────┐
    │ INIT │  Object constructed, COM CoInitializeEx() called
    └──┬───┘
       │ connect() succeeds
       ▼
  ┌───────────┐
  │ CONNECTED │  CoCreateInstance + CreateEx succeeded, device set created
  └─────┬─────┘
        │ open_devices() succeeds
        ▼
   ┌───────────┐
   │ STREAMING │  OpenDevice succeeded, command loop running
   └─────┬─────┘
         │ VD_E_CLOSE received     │ sink write failure / timeout / abort
         ▼                          ▼
   ┌───────────┐            ┌──────────┐
   │ FLUSHING  │            │  FAILED  │  Unrecoverable error
   └─────┬─────┘            └────┬─────┘
         │ close() called        │ close() called
         ▼                        ▼
   ┌──────────┐            ┌──────────┐
   │  CLOSED  │            │  CLOSED  │  Always end at CLOSED
   └──────────┘            └──────────┘
```

### State Transition Rules

| From | To | Trigger |
|------|----|---------|
| INIT | CONNECTED | `connect()` returns true |
| INIT | FAILED | `connect()` returns false |
| CONNECTED | STREAMING | `open_devices()` returns true |
| CONNECTED | FAILED | `open_devices()` or `GetConfiguration` fails |
| STREAMING | FLUSHING | `GetCommand` returns `VD_E_CLOSE` or `VD_E_EOF` |
| STREAMING | FAILED | Sink write fails, `CompleteCommand` fails, or `VD_E_ABORT` |
| FLUSHING | CLOSED | `close()` completes successfully |
| FAILED | CLOSED | `close()` called for cleanup |
| any | CLOSED | `close()` called (idempotent) |

### Invalid Transition Guards

All public API methods check for valid state transitions and log a clear error
message with the current and expected states.

---

## 2. Shutdown Sequencing

Shutdown follows a strict ordered sequence to avoid race conditions, partial
writes, and teardown crashes.

```
1. STOP COMMAND PROCESSING
   └─ Set `running = false` in process_commands() loop
   └─ Close signal (VD_E_CLOSE or VDC_Close)

2. FLUSH SINK
   └─ sink_->flush() — drains any buffered data
   └─ For FileSink: calls FlushFileBuffers for durability

3. RELEASE DEVICE INTERFACES
   └─ For each IClientVirtualDevice*: device->Release()
   └─ Clear vector

4. CLOSE DEVICE SET
   └─ device_set_->Close()
   └─ device_set_->Release()
   └─ Set to nullptr

5. MARK CLOSED
   └─ devices_closed_ = true
   └─ state_ = SessionState::CLOSED
```

### Destructor Safety

If `close()` was never called explicitly, the destructor calls it automatically
(only if the session progressed beyond INIT state). A warning is logged in this
case to alert developers of missing explicit cleanup.

---

## 3. Timeout Handling

### GetCommand Timeout

- **Default:** 30 seconds (`DEFAULT_COMMAND_TIMEOUT_MS`)
- **Configuration:** `set_command_timeout(unsigned long timeout_ms)`
- **Effect:** When `GetCommand` times out (returns `VD_E_TIMEOUT`), the command
  loop logs a warning and calls the stall detector, then continues. A timeout
  does not abort the session — it may indicate a quiet period.

```cpp
// Example: Set to 5-minute timeout for long-running backups
client.set_command_timeout(300000);
// Example: Set to INFINITE to disable timeout (original behavior)
client.set_command_timeout(INFINITE);
```

### GetConfiguration Timeout

- **Hardcoded:** 60 seconds
- **Effect:** If SQL Server does not connect within this window,
  `GetConfiguration` returns `VD_E_TIMEOUT` and `open_devices()` returns false.

---

## 4. Cleanup Guarantees

| Resource | Guarantee | Protection |
|----------|-----------|------------|
| Device interfaces | Released exactly once | `devices_closed_` flag prevents double-release |
| Device set | Closed + Released exactly once | Same guard; null-checked |
| COM uninitialization | Called exactly once (destructor) | `CoUninitialize` in destructor |
| Sink flush | Called before device release | `close()` flushes sink if state was STREAMING/FLUSHING |
| Buffer pool | Freed by destructor | RAII — vector clears buffers |

### Idempotent close()

Calling `close()` multiple times is safe. The second call is a no-op:

```cpp
void VdiClient::close() {
    if (devices_closed_) {
        LOG_INFO("close() called again — already closed (idempotent).");
        return;
    }
    // ... cleanup ...
    devices_closed_ = true;
    state_ = SessionState::CLOSED;
}
```

---

## 5. Stall Detection

The `check_stall()` method monitors byte progress to detect hung connections
or blocked sinks.

### Detection Mechanism

1. **Timer:** Every `STALL_WARN_MS` (60 seconds), the stall detector checks
   whether `total_bytes` has changed.
2. **Alert:** If no progress was made, a `LOG_WARN` is emitted with the current
   byte count and session state.
3. **No self-healing:** The detector only warns — it does not abort the session.
   The operator can decide whether to intervene.

### When It Fires

- **Hung SQL Server:** SQL Server stopped sending data but hasn't closed
  the connection.
- **Blocked sink:** Sink write is slow or stuck, preventing the command loop
  from making progress.
- **Network partition:** If the VDI connection is interrupted without a clean
  VD_E_CLOSE.

---

## 6. Retry Policy

| Failure | Retry? | Rationale |
|---------|--------|-----------|
| `VD_E_CLOSE` (normal end) | No | Session complete — no data to retry |
| `VD_E_EOF` (end of stream) | No | Same as above |
| `VD_E_TIMEOUT` (GetCommand) | Maybe | Transient — loop continues automatically |
| `VD_E_TIMEOUT` (GetConfiguration) | Yes | User can retry after ensuring SQL Server is connected |
| Sink write failure | No | Data loss possible — escalate to operator |
| `CompleteCommand` failure | No | Protocol state uncertain — abort |
| Invalid `VDC_Write` command | No | Protocol corruption — abort |
| COM initialization failure (CoCreateInstance) | No | Environment issue — fatal |
| `CreateEx` failure | No | Configuration error — fatal |
| Sink `is_open()` false | No | Resource issue — abort |

### Design Principle

> "Fail fast on protocol errors, be lenient on transient stalls."

The VDI protocol is a deterministic FIFO channel. Retrying a failed
`CompleteCommand` or write risks data corruption because the protocol state
machine may be in an undefined position. Only timeouts and configuration delays
are candidates for retry — and even those are left to the operator, not
automated.

---

## 7. Sink Failure Propagation

Sink write failures are propagated to the VDI session state:

```cpp
bool write_ok = sink_->write(data, size);
if (!write_ok) {
    LOG_ERROR("Sink write failed — data loss possible.");
    state_ = SessionState::FAILED;
    running = false;
    break;
}
```

### Sink Health Check

Before each write, the sink is queried via `is_open()`:

```cpp
if (!sink_->is_open()) {
    LOG_ERROR("Sink is not open — cannot write data.");
    state_ = SessionState::FAILED;
    running = false;
    break;
}
```

### NullSink Behavior

`NullSink::write()` always returns true. `NullSink::is_open()` always returns
true. This ensures benchmarking runs are never derailed by sink issues.

### FileSink Failure

`FileSink::write()` returns false if:
- The file handle is invalid (failed to open, or already closed)
- The aligned buffer allocation failed
- `WriteFile()` returns FALSE (disk full, I/O error)
- Number of bytes written does not match the request

---

## 8. Fault Injection Testing

The `FaultInjector` class provides compile-time gated failure hooks.

```cpp
// Enable in a test build by defining FAULT_INJECTION_ENABLED=1
#define FAULT_INJECTION_ENABLED 1
#include "fault_injector.h"
```

### Modes

| Mode | Method | Behavior |
|------|--------|----------|
| After N chunks | `set_fail_after_n_chunks(100)` | After 100 successful completions, the next `on_chunk_completed()` returns true |
| Next command | `set_fail_next_command()` | Single-shot: the very next `should_fail()` returns true |

### Usage in Tests

```cpp
// Arrange: inject failure after 500 chunks
client.fault_injector().set_fail_after_n_chunks(500);

// Act: run backup (process_commands will abort at chunk 500)
// Assert: state is FAILED, cleanup ran correctly
assert(client.state() == SessionState::CLOSED);
```

### Zero-Cost When Disabled

When `FAULT_INJECTION_ENABLED` is 0 (the default), all `FaultInjector` methods
are empty inline stubs. No state is allocated, no branches are emitted.

---

## 9. Resource Accounting

The `VdiClient` tracks resource usage during the backup session:

| Counter | Type | What It Tracks |
|---------|------|----------------|
| `active_buffers_` | atomic uint64 | Number of buffers currently acquired from the pool |
| `max_active_buffers_` | uint64 | High-water mark of concurrently acquired buffers |

These are logged at the end of `process_commands()`:

```text
[INFO]  Resource accounting: max_active_buffers=3
```

### What It Detects

- **Buffer leaks:** If releases don't match acquires, the counter drifts.
- **Pool exhaustion:** If `max_active_buffers_` equals pool size (64),
  the pool was fully consumed at some point.
- **Protocol misuse:** Unexpected acquire/release patterns.

---

## Appendix: Error Code Reference

| Symbol | Value | Meaning | Action |
|--------|-------|---------|--------|
| `VD_E_CLOSE` | 0x80770001 | Normal session end | Flush sink, close devices |
| `VD_E_TIMEOUT` | 0x80770003 | Operation timed out | Log warning, continue or retry |
| `VD_E_ABORT` | 0x80770004 | Operation aborted | Set FAILED, abort command loop |
| `VD_E_INVALID` | 0x80770006 | Invalid parameter | Set FAILED, check configuration |
| `VD_E_NOTSUPPORTED` | 0x80770009 | Feature not supported | Set FAILED, check CreateEx params |
| `VD_E_OBJECT` | 0x8077000C | Object state error | Set FAILED, check call order |
| `VD_E_EOF` | 0x8077000E | End of data stream | Same as VD_E_CLOSE |

---

*Last updated: 2026-05-17*