#include "vdi_client.h"
#include "logging.h"
#include <chrono>
#include <iomanip>

const char* command_to_string(DWORD cmd) {
    switch (cmd) {
    case VDC_Read:       return "VDC_Read";
    case VDC_Write:      return "VDC_Write";
    case VDC_Flush:      return "VDC_Flush";
    case VDC_Close:      return "VDC_Close";
    default:             return "UNKNOWN";
    }
}

VdiClient::VdiClient(std::unique_ptr<Sink> sink,
                     bool enable_logging,
                     TimerMode timer_mode)
    : device_set_(nullptr),
      current_cmd_(nullptr),
      sink_(std::move(sink)),
      metrics_(std::unique_ptr<Timer>(create_timer(timer_mode)), enable_logging),
      logging_enabled_(enable_logging),
      state_(SessionState::INIT),
      devices_closed_(false),
      command_timeout_ms_(DEFAULT_COMMAND_TIMEOUT_MS),
      last_command_us_(0),
      last_bytes_total_(0),
      last_stall_warn_us_(0),
      active_buffers_(0),
      max_active_buffers_(0) {

#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(
        NULL,
        COINIT_MULTITHREADED);

    if (FAILED(hr)) {
        LOG_ERROR("COM init failed: %s",
                   hresult_to_string(hr).c_str());
        state_ = SessionState::FAILED;
    }
#endif
}

VdiClient::~VdiClient() {
    // Ensure cleanup happens even if close() was never called explicitly
    if (state_ != SessionState::CLOSED && state_ != SessionState::INIT) {
        LOG_WARN("VDI session destroyed without explicit close() call");
        close();
    }

#if defined(_WIN32)
    CoUninitialize();
#endif
}

void VdiClient::set_command_timeout(unsigned long timeout_ms) {
    command_timeout_ms_ = timeout_ms;
    LOG_INFO("GetCommand timeout set to %lu ms", timeout_ms);
}

bool VdiClient::connect(const std::wstring& device_name, int device_count) {
    // ── State guard: only INIT → CONNECTED ──────────────────────────────
    if (state_ != SessionState::INIT) {
        LOG_ERROR("connect() called in state %s (expected INIT)",
                   session_state_to_string(state_));
        return false;
    }

    device_name_ = device_name;
    device_count_ = device_count;

#if defined(_WIN32)
    HRESULT hr = CoCreateInstance(
        CLSID_MSSQL_ClientVirtualDeviceSet,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IClientVirtualDeviceSet2,
        (void**)&device_set_
    );

    if (FAILED(hr)) {
        LOG_ERROR("CoCreateInstance failed: %s",
                   hresult_to_string(hr).c_str());
        state_ = SessionState::FAILED;
        return false;
    }

    VDConfig config = {};
    config.deviceCount = device_count;
    config.features = VDF_Default;
    config.prefixZoneSize = 0;
    config.alignment = 0;
    config.softFileMarkBlockSize = 0;
    config.EOMWarningSize = 0;
    config.serverTimeOut = 60000;
    config.blockSize = 64 * 1024;
    config.maxIODepth = 0;
    config.bufferAreaSize = 4 * 1024 * 1024;
    config.maxTransferSize = 1024 * 1024;

    hr = device_set_->CreateEx(nullptr, device_name.c_str(), &config);
    if (FAILED(hr)) {
        LOG_ERROR("CreateEx failed: %s",
                   hresult_to_string(hr).c_str());
        state_ = SessionState::FAILED;
        return false;
    }

    LOG_INFO("VDI device set created successfully.");
    LOG_INFO("Device set name: \"%ws\"", device_name_.c_str());
    LOG_INFO("Device count: %d", device_count_);

    state_ = SessionState::CONNECTED;
    return true;
#else
    LOG_INFO("VDI device set created successfully.");
    state_ = SessionState::CONNECTED;
    return true;
#endif
}

bool VdiClient::open_devices() {
    // ── State guard: only CONNECTED → STREAMING ─────────────────────────
    if (state_ != SessionState::CONNECTED) {
        LOG_ERROR("open_devices() called in state %s (expected CONNECTED)",
                   session_state_to_string(state_));
        return false;
    }

#if defined(_WIN32)
    if (!device_set_) {
        LOG_ERROR("No device set. Call connect() first.");
        state_ = SessionState::FAILED;
        return false;
    }

    // Phase 1: GetConfiguration - this waits for SQL Server to connect
    VDConfig server_config = {};
    LOG_INFO("Waiting for SQL Server to connect to device set \"%ws\"...",
             device_name_.c_str());
    LOG_INFO("(Execute: BACKUP DATABASE [db] TO VIRTUAL_DEVICE = N'%ws')",
             device_name_.c_str());

    HRESULT hr = device_set_->GetConfiguration(60000, &server_config);
    if (FAILED(hr)) {
        LOG_ERROR("GetConfiguration failed: %s",
                   hresult_to_string(hr).c_str());
        state_ = SessionState::FAILED;
        return false;
    }

    LOG_INFO("SQL Server connected. Server configuration:");
    LOG_INFO("  deviceCount:    %lu", server_config.deviceCount);
    LOG_INFO("  features:       0x%lx", server_config.features);
    LOG_INFO("  serverTimeOut:  %lu", server_config.serverTimeOut);
    LOG_INFO("  blockSize:      %lu", server_config.blockSize);
    LOG_INFO("  maxTransferSize: %lu", server_config.maxTransferSize);

    // Phase 2: OpenDevice for each virtual device.
    for (int i = 0; i < static_cast<int>(server_config.deviceCount); ++i) {

        IClientVirtualDevice* device = nullptr;

        hr = device_set_->OpenDevice(
            device_name_.c_str(),
            &device);

        if (FAILED(hr)) {
            LOG_ERROR("OpenDevice failed: %s",
                       hresult_to_string(hr).c_str());
            state_ = SessionState::FAILED;
            return false;
        }

        devices_.push_back(device);

        LOG_INFO("Opened virtual device %d: \"%ws\"", i, device_name_.c_str());
    }

    state_ = SessionState::STREAMING;
    return true;
#else
    state_ = SessionState::STREAMING;
    return true;
#endif
}

void VdiClient::close() {
    // ── Idempotent close guard ──────────────────────────────────────────
    if (devices_closed_) {
        LOG_INFO("close() called again — already closed (idempotent).");
        return;
    }

    LOG_INFO("Closing VDI session (state=%s)...",
             session_state_to_string(state_));

    // Flush sink before releasing devices
    if (sink_ && (state_ == SessionState::STREAMING ||
                  state_ == SessionState::FLUSHING)) {
        LOG_INFO("Flushing sink before device close...");
        sink_->flush();
    }

    // Release all device interfaces
    for (auto device : devices_) {
        device->Release();
    }
    devices_.clear();

    // Close and release the device set
    if (device_set_) {
        device_set_->Close();
        device_set_->Release();
        device_set_ = nullptr;
    }

    devices_closed_ = true;
    state_ = SessionState::CLOSED;

    LOG_INFO("VDI session closed cleanly.");
}

// ── Per-chunk data flow ─────────────────────────────────────────────────
// For each chunk:
//   1. GetCommand()         → timed as getcommand_us
//   2. Dispatch (switch)    → timed as dispatch_us
//   3. Sink write           → timed as sink_us (only VDC_Write)
//   4. CompleteCommand()    → timed as complete_us
// ────────────────────────────────────────────────────────────────────────

void VdiClient::process_commands() {

    // ── State guard: only STREAMING → process → FLUSHING/CLOSED ─────────
    if (state_ != SessionState::STREAMING) {
        LOG_ERROR("process_commands() called in state %s (expected STREAMING)",
                   session_state_to_string(state_));
        return;
    }

    bool running = true;
    uint64_t session_start_us = metrics_.now_us();
    last_command_us_ = session_start_us;
    last_bytes_total_ = 0;
    last_stall_warn_us_ = session_start_us;

    while (running) {

        for (auto device : devices_) {

            VDC_Command* cmd = nullptr;

            // ── Stage 1: GetCommand with configurable timeout ───────────
            uint64_t gc_start = metrics_.now_us();

            HRESULT hr = device->GetCommand(
                command_timeout_ms_,
                &cmd);

            uint64_t gc_end = metrics_.now_us();
            uint64_t gc_us = gc_end - gc_start;

            // Update stall detection timestamp
            last_command_us_ = gc_end;

            // Record GetCommand latency (legacy)
            metrics_.record_getcommand_latency(
                std::chrono::microseconds(gc_us));

            if (FAILED(hr)) {
                if (hr == VD_E_CLOSE) {
                    LOG_INFO("GetCommand returned VD_E_CLOSE — backup complete, initiating graceful shutdown.");
                    // Graceful shutdown: flush sink, then transition
                    state_ = SessionState::FLUSHING;
                    if (sink_) {
                        LOG_INFO("Flushing sink on VD_E_CLOSE...");
                        sink_->flush();
                    }
                    running = false;
                    break;
                } else if (hr == VD_E_EOF) {
                    LOG_INFO("GetCommand returned VD_E_EOF — backup complete.");
                    state_ = SessionState::FLUSHING;
                    if (sink_) {
                        sink_->flush();
                    }
                    running = false;
                    break;
                } else if (hr == VD_E_TIMEOUT) {
                    LOG_WARN("GetCommand timed out after %lu ms — checking progress...",
                              command_timeout_ms_);
                    check_stall(gc_end);
                    // Continue the loop — a timeout may just mean a quiet period
                    continue;
                } else {
                    LOG_ERROR("GetCommand failed: %s",
                               hresult_to_string(hr).c_str());
                    state_ = SessionState::FAILED;
                }
                running = false;
                break;
            }

            if (!cmd) {
                // Null command with success HRESULT is unexpected
                LOG_WARN("GetCommand returned S_OK with null command — continuing...");
                continue;
            }

            LOG_DEBUG("Received command: %s (code=%lu)",
                       command_to_string(cmd->commandCode),
                       cmd->commandCode);

            // ── Stage 2: Dispatch (command dispatch + non-write work) ──
            uint64_t dispatch_start = metrics_.now_us();
            bool should_complete = handle_command(device, cmd);
            uint64_t dispatch_end = metrics_.now_us();
            uint64_t dispatch_us = dispatch_end - dispatch_start;

            // Check if session entered FAILED state (e.g. sink write failure)
            if (state_ == SessionState::FAILED) {
                LOG_ERROR("Session entered FAILED state — aborting command loop.");
                // Do NOT call CompleteCommand — device may be in an invalid state
                running = false;
                break;
            }

            // VDC_Close: break out (no CompleteCommand)
            if (!should_complete) {
                running = false;
                break;
            }

            // ── Stage 3: Sink write (only for VDC_Write) ───────────────
            uint64_t sink_us = 0;
            if (cmd->commandCode == VDC_Write && sink_) {

                // Validate the write command before touching the buffer
                if (!validate_write_command(cmd)) {
                    LOG_ERROR("Invalid VDC_Write command detected — aborting.");
                    state_ = SessionState::FAILED;
                    running = false;
                    break;
                }

                // Check sink health before writing
                if (!sink_->is_open()) {
                    LOG_ERROR("Sink is not open — cannot write data.");
                    state_ = SessionState::FAILED;
                    running = false;
                    break;
                }

                uint64_t sink_start = metrics_.now_us();
                {
                    TRACE_EVENT("SinkWrite");
                    bool write_ok = sink_->write(
                        static_cast<uint8_t*>(cmd->buffer),
                        cmd->size);

                    if (!write_ok) {
                        LOG_ERROR("Sink write failed — data loss possible.");
                        state_ = SessionState::FAILED;
                        running = false;
                        break;
                    }
                }
                uint64_t sink_end = metrics_.now_us();
                sink_us = sink_end - sink_start;

                metrics_.record_sink_latency(
                    std::chrono::microseconds(sink_us));

                // Update total bytes for stall detection
                last_bytes_total_ = metrics_.total_bytes();
            }

            if (!running) break;  // exit loop if sink failure occurred

            // ── Stage 4: CompleteCommand ───────────────────────────────
            uint64_t cc_start = metrics_.now_us();

            hr = device->CompleteCommand(cmd, ERROR_SUCCESS, cmd->size, cmd->position);

            uint64_t cc_end = metrics_.now_us();
            uint64_t cc_us = cc_end - cc_start;

            metrics_.record_completecommand_latency(
                std::chrono::microseconds(cc_us));

            if (FAILED(hr)) {
                LOG_ERROR("CompleteCommand failed: %s",
                           hresult_to_string(hr).c_str());
                state_ = SessionState::FAILED;
                running = false;
                break;
            }

            // ── Fault injection hook ────────────────────────────────────
            if (fault_injector_.on_chunk_completed()) {
                LOG_WARN("Fault injector triggered after %llu chunks — simulating failure.",
                          static_cast<unsigned long long>(fault_injector_.chunk_count()));
                state_ = SessionState::FAILED;
                running = false;
                break;
            }

            // ── Record full ChunkTiming ────────────────────────────────
            uint64_t chunk_size = static_cast<uint64_t>(cmd->size);
            uint64_t total_us = gc_us + dispatch_us + sink_us + cc_us;
            uint64_t timestamp_us = gc_start - session_start_us;

            ChunkTiming timing;
            timing.size = chunk_size;
            timing.getcommand_us = gc_us;
            timing.dispatch_us = dispatch_us;
            timing.sink_us = sink_us;
            timing.complete_us = cc_us;
            timing.total_us = total_us;
            timing.timestamp_us = timestamp_us;

            metrics_.record_chunk_timing(timing);
        }
    }

    // ── Post-loop cleanup ────────────────────────────────────────────────
    // If we transitioned to FLUSHING from VD_E_CLOSE, complete cleanup
    if (state_ == SessionState::FLUSHING) {
        close();
    }

    // Print human-readable summary
    metrics_.print_summary();

    // Output machine-readable JSON for benchmark scripts
    LOG_INFO("---BEGIN JSON METRICS---");
    LOG_INFO("%s", metrics_.to_json().c_str());
    LOG_INFO("---END JSON METRICS---");

    // Log resource accounting summary
    LOG_INFO("Resource accounting: max_active_buffers=%llu",
              static_cast<unsigned long long>(max_active_buffers_));
}

bool VdiClient::validate_write_command(const VDC_Command* cmd) {
    // Never trust protocol data blindly.
    //
    // Validate:
    //   - Non-null buffer pointer
    //   - Sane size (> 0, reasonable upper bound)
    //   - Reasonable position offset

    if (!cmd) {
        LOG_ERROR("validate_write_command: cmd is null");
        return false;
    }

    if (!cmd->buffer) {
        LOG_ERROR("validate_write_command: buffer pointer is null");
        return false;
    }

    if (cmd->size == 0) {
        LOG_ERROR("validate_write_command: chunk size is 0");
        return false;
    }

    // 1 GB upper bound — VDI protocol typically uses 1 MB max transfer size,
    // but we set a generous safety limit to catch corrupt protocol data.
    constexpr DWORD MAX_SANE_CHUNK_SIZE = 1024 * 1024 * 1024;  // 1 GB
    if (cmd->size > MAX_SANE_CHUNK_SIZE) {
        LOG_ERROR("validate_write_command: chunk size %lu exceeds sanity limit %lu",
                   cmd->size, MAX_SANE_CHUNK_SIZE);
        return false;
    }

    return true;
}

void VdiClient::check_stall(uint64_t now_us) {
    // Stall detection: if no byte progress has been made in the
    // STALL_WARN_MS window, log a warning.
    //
    // This detects:
    //   - Hung SQL Server connections
    //   - Blocked sinks
    //   - Protocol deadlocks

    if (last_stall_warn_us_ == 0) {
        last_stall_warn_us_ = now_us;
        return;
    }

    uint64_t elapsed_us = now_us - last_stall_warn_us_;
    if (elapsed_us < static_cast<uint64_t>(STALL_WARN_MS) * 1000) {
        return;  // Not enough time elapsed since last check
    }

    // Check if bytes have progressed
    uint64_t current_bytes = metrics_.total_bytes();
    if (current_bytes == last_bytes_total_) {
        LOG_WARN("Possible stall detected — no byte progress for %lu ms "
                  "(total_bytes=%llu, state=%s)",
                  STALL_WARN_MS,
                  static_cast<unsigned long long>(current_bytes),
                  session_state_to_string(state_));
        // Reset warning timer to avoid repeated logging every microsecond
        last_stall_warn_us_ = now_us;
    } else {
        // Progress was made; reset tracking
        last_bytes_total_ = current_bytes;
        last_stall_warn_us_ = now_us;
    }
}

bool VdiClient::handle_command(
    IClientVirtualDevice* device,
    VDC_Command* cmd) {

    switch (cmd->commandCode) {

    case VDC_Read:
        LOG_DEBUG("  [VDC_Read]");
        // SQL Server requesting data read (not expected during pure BACKUP)
        break;

    case VDC_Write: {
        // Record chunk size metrics (min/max/avg) and bytes before sink write
        metrics_.record_chunk_size(cmd->size);
        metrics_.add_bytes(cmd->size);

        LOG_DEBUG("[WRITE] size=%zu", cmd->size);

        // Buffer pool instrumentation: simulate acquire/release
        {
            TRACE_EVENT("BufferPool");
            uint8_t* pool_buf = buffer_pool_.acquire();
            bool acquired = (pool_buf != nullptr);
            metrics_.record_buffer_acquire(acquired);

            if (acquired) {
                active_buffers_.fetch_add(1);
                uint64_t cur = active_buffers_.load();
                if (cur > max_active_buffers_) {
                    max_active_buffers_ = cur;
                }

                buffer_pool_.release(pool_buf);
                active_buffers_.fetch_sub(1);
            }
            metrics_.record_buffer_release();
        }
        // Note: actual sink write happens in process_commands() stage 3
        // for independent timing.
        break;
    }

    case VDC_Flush:
        LOG_DEBUG("  [VDC_Flush]");
        if (sink_) {
            sink_->flush();
        }
        break;

    case VDC_ClearError:
        LOG_DEBUG("  [VDC_ClearError]");
        break;

    case VDC_Close:
        LOG_INFO("  [VDC_Close]");
        return false;  // Signal caller to break out of loop (no CompleteCommand)

    default:
        LOG_DEBUG("  [UNKNOWN COMMAND: %lu]", cmd->commandCode);
        break;
    }

    return true;  // Signal caller to call CompleteCommand + sink write timing
}