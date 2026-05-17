#pragma once

// ---------------------------------------------------------------------------
// VDI Client — high-level API for SQL Server VDI backup streaming.
//
// Public API:
//   VdiClient(std::unique_ptr<Sink> sink, ...)
//   bool connect(device_name, device_count)
//   bool open_devices()
//   void process_commands()
//   void close()
//   void set_command_timeout(ms)
//   const Metrics& metrics()
//   SessionState state()
//   FaultInjector& fault_injector()
//
// All VDI protocol types (VDC_Command, VDConfig, COM interfaces) are
// isolated in internal implementation files. The public header exposes
// only opaque void* pointers for COM handles.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>

#include "sink.h"
#include "metrics.h"
#include "buffer_pool.h"
#include "timer.h"
#include "error_utils.h"
#include "fault_injector.h"

// Configuration constants
constexpr size_t BUFFER_POOL_SIZE = 64;
constexpr size_t BUFFER_SIZE = 64 * 1024;

// Timeout for GetCommand (ms). Replace INFINITE to prevent protocol stalls.
// Can be overridden at runtime via set_command_timeout().
constexpr unsigned long DEFAULT_COMMAND_TIMEOUT_MS = 30000;

// Stall detection: if no byte progress for this many milliseconds, log a warning.
constexpr unsigned long STALL_WARN_MS = 60000;

class VdiClient {
public:
    VdiClient(std::unique_ptr<Sink> sink,
              bool enable_logging = false,
              TimerMode timer_mode = TimerMode::Chrono);
    ~VdiClient();

    // No copy or move
    VdiClient(const VdiClient&) = delete;
    VdiClient& operator=(const VdiClient&) = delete;
    VdiClient(VdiClient&&) = delete;
    VdiClient& operator=(VdiClient&&) = delete;

    // ── Public API ──────────────────────────────────────────────────────
    // Connect to a VDI device set. Blocks until SQL Server connects or
    // the operation times out (default: 60 s).
    bool connect(const std::wstring& device_name, int device_count);

    // Open virtual devices after SQL Server has connected.
    // Blocks until GetConfiguration succeeds.
    bool open_devices();

    // Run the blocking command loop (backup data flow).
    // Blocks until backup completes and prints summary / JSON metrics.
    void process_commands();

    // Close all open devices and release COM resources.
    // Safe to call multiple times (idempotent).
    void close();

    // ── Configuration ───────────────────────────────────────────────────
    // Set the GetCommand timeout in milliseconds.
    // Default: DEFAULT_COMMAND_TIMEOUT_MS (30 s).
    // Set to INFINITE (0xFFFFFFFF) to disable timeout (original behavior).
    void set_command_timeout(unsigned long timeout_ms);

    // ── Diagnostics ─────────────────────────────────────────────────────
    const Metrics& metrics() const { return metrics_; }

    // Current session state (for testing and diagnostics)
    SessionState state() const { return state_; }

    // Fault injector access (for testing)
    FaultInjector& fault_injector() { return fault_injector_; }

private:
    // Update stall detection state. Logs a warning if no byte progress
    // has been made within the stall detection window.
    void check_stall(uint64_t now_us);

    // Opaque COM handles — cast to actual types in the .cpp implementation.
    // Using void* avoids forward-declaration conflicts with COM interface
    // keyword definitions in sqlvdi.h.
    void* device_set_;

    // Sink and metrics
    std::unique_ptr<Sink> sink_;
    Metrics metrics_;
    BufferPool buffer_pool_{BUFFER_SIZE, BUFFER_POOL_SIZE};

    // Session parameters
    std::wstring device_name_;
    int device_count_;

    // Logging control
    bool logging_enabled_;

    // ── Session state machine ───────────────────────────────────────────
    SessionState state_;

    // ── Cleanup guard (idempotent close) ────────────────────────────────
    bool devices_closed_;

    // ── GetCommand timeout (ms) ─────────────────────────────────────────
    unsigned long command_timeout_ms_;

    // ── Stall detection ─────────────────────────────────────────────────
    uint64_t last_command_us_;
    uint64_t last_bytes_total_;
    uint64_t last_stall_warn_us_;

    // ── Resource accounting ─────────────────────────────────────────────
    std::atomic<uint64_t> active_buffers_;
    uint64_t max_active_buffers_;

    // ── Fault injection hook ────────────────────────────────────────────
    FaultInjector fault_injector_;
};