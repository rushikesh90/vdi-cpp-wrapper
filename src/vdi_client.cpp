#include "vdi_client.h"
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

void log_hresult(const char* msg, HRESULT hr) {
    std::cerr << msg
              << " HRESULT=0x"
              << std::hex << hr
              << std::dec << "\n";
}

VdiClient::VdiClient() : device_set_(nullptr), current_cmd_(nullptr) {}

VdiClient::~VdiClient() {
#if defined(_WIN32)
    if (device_set_) {
        device_set_->Release();
        device_set_ = nullptr;
    }
    CoUninitialize();
#endif
}

bool VdiClient::connect(const std::wstring& device_name, int device_count) {
    device_name_ = device_name;
    device_count_ = device_count;

#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        log_hresult("CoInitializeEx failed", hr);
        return false;
    }

    hr = CoCreateInstance(
        CLSID_MSSQL_ClientVirtualDeviceSet,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IClientVirtualDeviceSet2,
        (void**)&device_set_
    );

    if (FAILED(hr)) {
        log_hresult("CoCreateInstance(CLSID_MSSQL_ClientVirtualDeviceSet) failed", hr);
        return false;
    }

    VDConfig config = {};
    config.deviceCount = device_count;
    config.features = VDF_LikePipe;
    config.prefixZoneSize = 0;
    config.alignment = 0;
    config.softFileMarkBlockSize = 0;
    config.EOMWarningSize = 0;
    config.serverTimeOut = 60000;
    config.blockSize = 0;
    config.maxIODepth = 0;
    config.maxTransferSize = 0;
    config.bufferAreaSize = 0;

    hr = device_set_->CreateEx(nullptr, device_name.c_str(), &config);
    if (FAILED(hr)) {
        log_hresult("CreateEx failed", hr);
        if (hr == 0x80770009) {
            std::wcerr << L"Hint: VD_E_NOTSUPPORTED - check VDConfig or configuration parameters.\n";
        }
        return false;
    }

    std::cout << "VDI device set created successfully.\n";
    std::cout << "Device set name: \"" << device_name_.c_str() << "\"\n";
    std::cout << "Device count: " << device_count_ << "\n";
    return true;
#else
    // Non-Windows placeholder - always succeeds
    std::cout << "VDI device set created successfully\n";
    return true;
#endif
}

bool VdiClient::open_devices() {
#if defined(_WIN32)
    if (!device_set_) {
        std::cerr << "No device set. Call connect() first.\n";
        return false;
    }

    // Phase 1: GetConfiguration - this waits for SQL Server to connect
    // to the named device set and negotiate configuration.
    // If SQL Server is not connected, this will block until serverTimeOut
    // and then fail with VD_E_TIMEOUT.
    VDConfig server_config = {};
    std::cout << "Waiting for SQL Server to connect to device set \"" << device_name_.c_str() << "\"...\n";
    std::cout << "(Execute: BACKUP DATABASE [db] TO VIRTUAL_DEVICE = N'" << device_name_.c_str() << "')\n";

    HRESULT hr = device_set_->GetConfiguration(60000, &server_config);
    if (FAILED(hr)) {
        log_hresult("GetConfiguration failed", hr);
        std::wcerr << L"Make sure SQL Server is running and a BACKUP ... TO VIRTUAL_DEVICE command\n";
        std::wcerr << L"has been issued with device name \"" << device_name_.c_str() << L"\"\n";
        if (hr == 0x8077000c) {
            std::wcerr << L"Hint: VD_E_OBJECT - device set not ready. Connect SQL Server first.\n";
        }
        return false;
    }

    std::cout << "SQL Server connected. Server configuration:\n";
    std::cout << "  deviceCount: " << server_config.deviceCount << "\n";
    std::cout << "  features: 0x" << std::hex << server_config.features << std::dec << "\n";
    std::cout << "  serverTimeOut: " << server_config.serverTimeOut << "\n";
    std::cout << "  blockSize: " << server_config.blockSize << "\n";
    std::cout << "  maxTransferSize: " << server_config.maxTransferSize << "\n";

    // Phase 2: OpenDevice for each virtual device.
    // The device name passed to OpenDevice must match the name used
    // in BACKUP DATABASE ... TO VIRTUAL_DEVICE = N'<name>' in T-SQL.
    // For deviceCount > 1, each device uses the same base name.
    for (int i = 0; i < static_cast<int>(server_config.deviceCount); ++i) {

        IClientVirtualDevice* device = nullptr;

        hr = device_set_->OpenDevice(
            device_name_.c_str(),
            &device);

        if (FAILED(hr)) {
            log_hresult("OpenDevice failed", hr);
            return false;
        }

        devices_.push_back(device);

        std::wcout << L"Opened virtual device " << i << L": \"" << device_name_ << L"\"\n";
    }

    return true;
#else
    // Non-Windows placeholder - always succeeds
    return true;
#endif
}

bool VdiClient::get_next_chunk(uint8_t*& data, size_t& size) {
    data = nullptr;
    size = 0;

    if (devices_.empty()) {
        return false;
    }

    // GetCommand from the first virtual device (deviceCount=1)
    // Blocks waiting for SQL Server to issue a command
    auto device = devices_[0];
    VDC_Command* cmd = nullptr;

    HRESULT hr = device->GetCommand(INFINITE, &cmd);
    if (FAILED(hr)) {
        log_hresult("GetCommand failed", hr);
        return false;
    }

    current_cmd_ = cmd;

    switch (cmd->commandCode) {
        case VDC_Write:
            data = cmd->buffer;
            size = cmd->size;
            return true;

        case VDC_Flush:
            // Flush: just complete immediately
            return true;

        case VDC_ClearError:
            // Clear error: just complete immediately
            return true;

        default:
            // For any other command (VDC_Rewind, VDC_Read, VDC_Load, etc.),
            // we process the buffer if any
            if (cmd->size > 0 && cmd->buffer) {
                data = cmd->buffer;
                size = cmd->size;
            }
            return true;
    }
}

void VdiClient::complete_chunk() {
    if (!current_cmd_) {
        return;
    }

    auto cmd = static_cast<VDC_Command*>(current_cmd_);
    auto device = devices_[0];

    HRESULT hr = device->CompleteCommand(cmd, ERROR_SUCCESS, cmd->size, cmd->position);
    if (FAILED(hr)) {
        log_hresult("CompleteCommand failed", hr);
    }

    current_cmd_ = nullptr;
}

void VdiClient::close() {
    for (auto device : devices_) {
        device->Release();
    }
    devices_.clear();

    if (device_set_) {
        device_set_->Close();
    }
}

void VdiClient::process_commands() {

    bool running = true;

    while (running) {

        for (auto device : devices_) {

            VDC_Command* cmd = nullptr;

            HRESULT hr = device->GetCommand(
                INFINITE,
                &cmd);

            if (FAILED(hr)) {
                // VD_E_CLOSE (0x80770001) means SQL Server finished and
                // closed the connection.  This is the normal way the
                // command loop terminates in the real VDI protocol.
                if (hr == VD_E_CLOSE) {
                    std::cout << "GetCommand returned VD_E_CLOSE – backup complete.\n";
                } else {
                    log_hresult("GetCommand failed", hr);
                }
                running = false;
                break;
            }

            if (!cmd) {
                continue;
            }

            std::cout << "Received command: "
                      << command_to_string(cmd->commandCode)
                      << " (code=" << cmd->commandCode << ")\n";

            handle_command(device, cmd);

            if (cmd->commandCode == VDC_Close) {
                running = false;
            }
        }
    }
}

void VdiClient::handle_command(
    IClientVirtualDevice* device,
    VDC_Command* cmd) {

    HRESULT hr = S_OK;

    switch (cmd->commandCode) {

    case VDC_Read:
        std::cout << "  [" << command_to_string(cmd->commandCode) << "]\n";

        // SQL Server requesting data read
        // (not expected during a pure BACKUP; used for RESTORE)
        break;

    case VDC_Write:
        std::cout << "  [" << command_to_string(cmd->commandCode) << "] size="
                  << cmd->size << " position="
                  << cmd->position << "\n";

        // SQL Server sending backup data.
        // Data is in cmd->buffer, size is cmd->size.
        // In a real backup application we would write this to disk.
        break;

    case VDC_Flush:
        std::cout << "  [" << command_to_string(cmd->commandCode) << "]\n";
        break;

    case VDC_ClearError:
        std::cout << "  [" << command_to_string(cmd->commandCode) << "]\n";
        break;

    case VDC_Close:
        std::cout << "  [" << command_to_string(cmd->commandCode) << "] sentinel – completing session.\n";
        // VDC_Close (99) is our internal sentinel, not a real VDI command.
        // Do NOT call CompleteCommand for it.  Just return immediately.
        return;

    default:
        std::cout << "  [UNKNOWN COMMAND: " << cmd->commandCode << "]\n";
        break;
    }

    // CompleteCommand requires 4 arguments:
    //   (cmd, dwCompletionCode, dwBytesTransferred, dwlPosition)
    hr = device->CompleteCommand(cmd, ERROR_SUCCESS, cmd->size, cmd->position);

    if (FAILED(hr)) {
        log_hresult("CompleteCommand failed", hr);
    }
}