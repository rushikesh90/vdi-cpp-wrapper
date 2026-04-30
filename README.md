# VDI C++ Wrapper

Minimal C++ wrapper over SQL Server Virtual Device Interface (VDI) focused on performance and control.

## Motivation

Many SQL Server backup integrations rely on Python-based orchestration layers, which introduce:

- High syscall overhead (FFI / subprocess / IPC)
- Poor control over memory buffers and I/O scheduling
- Latency spikes during snapshot and data streaming phases

This project explores a native C++ alternative using Win32/COM to achieve:

- Deterministic performance
- Fine-grained buffer management
- Low-latency streaming

## Scope (Phase 1)

- Minimal VDI client
- Command loop abstraction
- Streaming to sink (file/null)

## Non-Goals

- Full backup system
- VSS orchestration
- Cloud storage integration

## Planned Architecture

[ SQL Server ]
        ↓ (VDI)
[ VDI Client ]
        ↓
[ Buffer Layer ]
        ↓
[ Sink ]
