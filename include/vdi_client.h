#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <combaseapi.h>
#include "sqlvdi.h"
#else
// Non-Windows placeholder definitions to allow compilation on other platforms
typedef void* IClientVirtualDeviceSet2;
typedef void* IClientVirtualDevice;
typedef int HRESULT;
#define S_OK 0
#define FAILED(hr) ((hr) != S_OK)
#define COINIT_MULTITHREADED 0
inline HRESULT CoInitializeEx(void*, unsigned int) { return S_OK; }
inline void CoUninitialize() {}
inline HRESULT CoCreateInstance(const void*, void*, unsigned int, const void*, void**) { return S_OK; }
#endif
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

    // ── Public API ──────────────────────────────────────────────────────
    // Connect to a VDI device set. Blocks until SQL Server connects or
    // the operation times out (default: 60 s).
    bool connect(const std::wstring& device_name, int device_count);

    // Open virtual devices and enter the command loop.
    // Blocks until backup completes and calls metrics_.print_summary()
    // and metrics_.to_json() on exit.
    void process_commands();

    // Close all open devices and release COM resources.
    // Safe to call multiple times (idempotent).
    void close();

    // ── Configuration ───────────────────────────────────────────────────
    // Set the GetCommand timeout in milliseconds.
    // Default: DEFAULT_COMMAND_TIMEOUT_MS (30 s).
    // Set to INFINITE (0xFFFFFFFF) to disable timeout (original behavior).
    void set_command_timeout(unsigned long timeout_ms);

    // ── Internal (public for testing) ───────────────────────────────────
    // Open the virtual devices after SQL Server has connected.
    bool open_devices();

    // Access to metrics (for post-benchmark inspection)
    const Metrics& metrics() const { return metrics_; }

    // Current session state (for testing and diagnostics)
    SessionState state() const { return state_; }

    // Fault injector access (for testing)
    FaultInjector& fault_injector() { return fault_injector_; }

private:
    // Handle a single command. Returns true if the command should be
    // completed (i.e. all non-Close commands). Returns false for VDC_Close
    // so the caller can break out of the command loop.
    // Sets state_ to FAILED on unrecoverable errors (e.g. sink write failure).
    bool handle_command(IClientVirtualDevice* device,
                        VDC_Command* cmd);

    // Validate that a VDC_Write command contains sane parameters.
    // Returns true if the command is valid, false otherwise.
    bool validate_write_command(const VDC_Command* cmd);

    // Update stall detection state. Logs a warning if no byte progress
    // has been made within the stall detection window.
    void check_stall(uint64_t now_us);

    IClientVirtualDeviceSet2* device_set_;
    void* current_cmd_;

    std::unique_ptr<Sink> sink_;
    Metrics metrics_;
    BufferPool buffer_pool_{BUFFER_SIZE, BUFFER_POOL_SIZE};

    std::vector<IClientVirtualDevice*> devices_;
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
    uint64_t last_command_us_;       // steady_clock micros at last GetCommand call
    uint64_t last_bytes_total_;      // total_bytes at last progress check
    uint64_t last_stall_warn_us_;    // steady_clock micros at last stall warning

    // ── Resource accounting ─────────────────────────────────────────────
    std::atomic<uint64_t> active_buffers_;     // buffers currently acquired
    uint64_t max_active_buffers_;              // high-water mark

    // ── Fault injection hook ────────────────────────────────────────────
    FaultInjector fault_injector_;
};