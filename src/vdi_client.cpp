#include "vdi_client.h"
#include "logging.h"
#include "com_runtime.h"
#include "version.h"
#include "vdi_protocol.h"
#include "sqlvdi.h"

#include <windows.h>
#include <combaseapi.h>
#include <chrono>
#include <iomanip>

VdiClient::VdiClient(std::unique_ptr<Sink> sink,
                     bool enable_logging,
                     TimerMode timer_mode)
    : device_set_(nullptr),
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
      max_active_buffers_(0)
{
}

VdiClient::~VdiClient() {
    // Ensure cleanup happens even if close() was never called explicitly
    if (state_ != SessionState::CLOSED && state_ != SessionState::INIT) {
        LOG_WARN("VDI session destroyed without explicit close() call");
        close();
    }
}

void VdiClient::set_command_timeout(unsigned long timeout_ms) {
    command_timeout_ms_ = timeout_ms;
    LOG_INFO("GetCommand timeout set to %lu ms", timeout_ms);
}

// ── connect(): INIT → CONNECTED | FAILED ────────────────────────────────

bool VdiClient::connect(const std::wstring& device_name, int device_count) {
    if (state_ != SessionState::INIT) {
        LOG_ERROR("connect() called in state %s (expected INIT)",
                   session_state_to_string(state_));
        return false;
    }

    device_name_ = device_name;
    device_count_ = device_count;

    auto com = std::make_unique<ComRuntime>();
    if (!com->initialized()) {
        LOG_ERROR("COM initialization failed — cannot connect.");
        state_ = SessionState::FAILED;
        return false;
    }

    // Keep com runtime alive for the connection scope (released when com goes
    // out of scope at end of this function — but close() will release COM at
    // session end). In practice CoUninitialize happens in ~ComRuntime.
    // For now we let it release here; the session continues until close().

#if defined(_WIN32)
    IClientVirtualDeviceSet2* ds = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MSSQL_ClientVirtualDeviceSet,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IClientVirtualDeviceSet2,
        (void**)&ds
    );

    if (FAILED(hr)) {
        LOG_ERROR("CoCreateInstance failed: %s",
                   hresult_to_string(hr).c_str());
        state_ = SessionState::FAILED;
        return false;
    }

    device_set_ = static_cast<void*>(ds);

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

    hr = ds->CreateEx(nullptr, device_name.c_str(), &config);
    if (FAILED(hr)) {
        LOG_ERROR("CreateEx failed: %s",
                   hresult_to_string(hr).c_str());
        ds->Release();
        device_set_ = nullptr;
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

// ── open_devices(): CONNECTED → STREAMING | FAILED ──────────────────────

bool VdiClient::open_devices() {
    if (state_ != SessionState::CONNECTED) {
        LOG_ERROR("open_devices() called in state %s (expected CONNECTED)",
                   session_state_to_string(state_));
        return false;
    }

#if defined(_WIN32)
    IClientVirtualDeviceSet2* ds = static_cast<IClientVirtualDeviceSet2*>(device_set_);
    if (!ds) {
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

    HRESULT hr = ds->GetConfiguration(60000, &server_config);
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

    state_ = SessionState::STREAMING;
    return true;
#else
    state_ = SessionState::STREAMING;
    return true;
#endif
}

// ── close(): any → CLOSED (idempotent) ──────────────────────────────────

void VdiClient::close() {
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

    // Close and release the device set
    if (device_set_) {
        IClientVirtualDeviceSet2* ds = static_cast<IClientVirtualDeviceSet2*>(device_set_);
        ds->Close();
        ds->Release();
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

    if (state_ != SessionState::STREAMING) {
        LOG_ERROR("process_commands() called in state %s (expected STREAMING)",
                   session_state_to_string(state_));
        return;
    }

    IClientVirtualDeviceSet2* ds = static_cast<IClientVirtualDeviceSet2*>(device_set_);
    if (!ds) {
        LOG_ERROR("No device set. Call open_devices() first.");
        return;
    }

    // Open devices — we do this here rather than in open_devices() to keep
    // device lifecycle local to the command loop.
    std::vector<IClientVirtualDevice*> devices;

    // We need to call OpenDevice for each device. Since we don't have the
    // server_config.deviceCount from open_devices(), we use device_count_.
    // In practice this is 1 for single-device backups.
    const int dev_count = (device_count_ > 0) ? device_count_ : 1;

    for (int i = 0; i < dev_count; ++i) {
        IClientVirtualDevice* device = nullptr;
        HRESULT hr = ds->OpenDevice(device_name_.c_str(), &device);
        if (FAILED(hr)) {
            LOG_ERROR("OpenDevice failed: %s",
                       hresult_to_string(hr).c_str());
            state_ = SessionState::FAILED;
            return;
        }
        devices.push_back(device);
        LOG_INFO("Opened virtual device %d: \"%ws\"", i, device_name_.c_str());
    }

    bool running = true;
    uint64_t session_start_us = metrics_.now_us();
    last_command_us_ = session_start_us;
    last_bytes_total_ = 0;
    last_stall_warn_us_ = session_start_us;

    while (running) {

        for (auto device : devices) {

            VDC_Command* cmd = nullptr;

            // ── Stage 1: GetCommand with configurable timeout ───────────
            uint64_t gc_start = metrics_.now_us();
            HRESULT hr = device->GetCommand(command_timeout_ms_, &cmd);
            uint64_t gc_end = metrics_.now_us();
            uint64_t gc_us = gc_end - gc_start;

            last_command_us_ = gc_end;

            if (FAILED(hr)) {
                if (hr == VD_E_CLOSE) {
                    LOG_INFO("GetCommand returned VD_E_CLOSE — backup complete, initiating graceful shutdown.");
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
                LOG_WARN("GetCommand returned S_OK with null command — continuing...");
                continue;
            }

            LOG_DEBUG("Received command: %s (code=%lu)",
                       command_code_string(cmd->commandCode).c_str(),
                       cmd->commandCode);

            // ── Stage 2: Dispatch ──────────────────────────────────────
            uint64_t dispatch_start = metrics_.now_us();
            CommandResult dispatch_result;
            unsigned long cmd_code = dispatch_command(device, cmd, dispatch_result);
            uint64_t dispatch_end = metrics_.now_us();
            uint64_t dispatch_us = dispatch_end - dispatch_start;

            if (dispatch_result == CommandResult::FAILED) {
                LOG_ERROR("Command dispatch failed — aborting command loop.");
                state_ = SessionState::FAILED;
                running = false;
                break;
            }

            if (dispatch_result == CommandResult::CLOSE_REQUEST) {
                running = false;
                break;
            }

            // ── Stage 3: Buffer pool simulation and sink write ──────────
            uint64_t sink_us = 0;
            if (cmd_code == VDC_Write) {

                metrics_.record_chunk_size(cmd->size);
                metrics_.add_bytes(cmd->size);

                // Buffer pool simulation (lightweight)
                {
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
                        metrics_.record_buffer_release();
                    }
                }

                if (!validate_write_command(cmd)) {
                    LOG_ERROR("Invalid VDC_Write command detected — aborting.");
                    state_ = SessionState::FAILED;
                    running = false;
                    break;
                }

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

                last_bytes_total_ = metrics_.total_bytes();
            }

            if (!running) break;

            // ── Stage 4: CompleteCommand ───────────────────────────────
            uint64_t cc_start = metrics_.now_us();
            hr = device->CompleteCommand(cmd, ERROR_SUCCESS, cmd->size, cmd->position);
            uint64_t cc_end = metrics_.now_us();
            uint64_t cc_us = cc_end - cc_start;

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

    // ── Release opened devices ───────────────────────────────────────────
    for (auto device : devices) {
        device->Release();
    }
    devices.clear();

    // ── Post-loop cleanup ────────────────────────────────────────────────
    if (state_ == SessionState::FLUSHING) {
        close();
    }

    metrics_.print_summary();

    LOG_INFO("---BEGIN JSON METRICS---");
    LOG_INFO("%s", metrics_.to_json().c_str());
    LOG_INFO("---END JSON METRICS---");

    LOG_INFO("Resource accounting: max_active_buffers=%llu",
              static_cast<unsigned long long>(max_active_buffers_));
}

void VdiClient::check_stall(uint64_t now_us) {
    if (last_stall_warn_us_ == 0) {
        last_stall_warn_us_ = now_us;
        return;
    }

    uint64_t elapsed_us = now_us - last_stall_warn_us_;
    if (elapsed_us < static_cast<uint64_t>(STALL_WARN_MS) * 1000) {
        return;
    }

    uint64_t current_bytes = metrics_.total_bytes();
    if (current_bytes == last_bytes_total_) {
        LOG_WARN("Possible stall detected — no byte progress for %lu ms "
                  "(total_bytes=%llu, state=%s)",
                  STALL_WARN_MS,
                  static_cast<unsigned long long>(current_bytes),
                  session_state_to_string(state_));
        last_stall_warn_us_ = now_us;
    } else {
        last_bytes_total_ = current_bytes;
        last_stall_warn_us_ = now_us;
    }
}