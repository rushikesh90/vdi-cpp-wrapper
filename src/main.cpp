// VDI Wrapper - Entry point (benchmark-ready)
//
// Usage:
//   vdi_wrapper_null              -- NullSink, no logging, interactive
//   vdi_wrapper_null --auto       -- NullSink, no logging, automated (sqlcmd)
//   vdi_wrapper_file --sink file  -- FileSink, no logging, interactive
//   vdi_wrapper_file --auto --sink file  -- FileSink, no logging, automated
//
// Interactive mode: runs, prints device name, waits for Enter key.
// Automated mode (--auto): invokes sqlcmd to issue BACKUP DATABASE
//   automatically using a configured benchmark database. Requires
//   sqlcmd on PATH.
//
// Build with LOGGING_LEVEL=0 for benchmarks.


#include "vdi_client.h"
#include "sink.h"       // NullSink
#include "file_sink.h"  // FileSink
#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>

// Launch sqlcmd asynchronously via start /B so it runs in the background
// without blocking the VDI client. Returns true on success.
bool launch_sqlcmd(const std::wstring& device_name, const std::wstring& backup_db) {
    // Build command: start "" /B sqlcmd ... (empty title, background, no window)
    std::wstring cmdline =
        L"cmd /c start \"\" /B sqlcmd -S (local) -E -d master -Q \"BACKUP DATABASE [" +
        backup_db +
        L"] TO VIRTUAL_DEVICE = N'" +
        device_name +
        L"'\" -b -t 300";

    int rc = _wsystem(cmdline.c_str());
    if (rc != 0) {
        std::cerr << "  Failed to launch sqlcmd (rc=" << rc << ")\n";
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
// --- CLI parsing ---
    bool use_file_sink = false;
    bool auto_mode = false;
    std::string benchmark_id = "default_run";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto") == 0) {
            auto_mode = true;
        } else if (std::strcmp(argv[i], "--sink") == 0 && i + 1 < argc) {
            if (std::strcmp(argv[i + 1], "file") == 0) {
                use_file_sink = true;
            }
            ++i;
        } else if (std::strcmp(argv[i], "--benchmark-id") == 0 && i + 1 < argc) {
            benchmark_id = argv[i + 1];
            ++i;
        }
    }

    // Generate unique device name using process ID
    DWORD pid = GetCurrentProcessId();
    std::wstring device_name = L"VDI_Device_" + std::to_wstring(pid);
    int device_count = 1;

    // Choose sink at runtime
    std::unique_ptr<Sink> sink;
    if (use_file_sink) {
        sink = std::make_unique<FileSink>("backup_stream.bin");
    } else {
        sink = std::make_unique<NullSink>();
    }

    VdiClient client(std::move(sink));

    // Database to back up (must exist and be backup-able)
    const std::wstring backup_db = L"VDIBenchmarkDB";

    const char* sink_name = use_file_sink ? "FileSink" : "NullSink";
    std::cout << "VDI C++ Wrapper\n";
    std::cout << "===============\n";
    std::cout << "Device name: " << std::string(device_name.begin(), device_name.end()) << "\n";
    std::cout << "Device count: " << device_count << "\n";
    std::cout << "Sink: " << sink_name << "\n";
    std::cout << "Database: " << std::string(backup_db.begin(), backup_db.end()) << "\n";
    std::cout << "Mode: " << (auto_mode ? "automated (sqlcmd)" : "interactive") << "\n\n";

    if (!client.connect(device_name, device_count)) {
        std::cerr << "Failed to connect.\n";
        return 1;
    }

    if (auto_mode) {
        // Launch sqlcmd ASYNCHRONOUSLY (non-blocking) so the VDI client
        // can proceed to GetConfiguration() which blocks waiting for SQL.
        std::cout << "  Launching: BACKUP DATABASE [" << std::string(backup_db.begin(), backup_db.end())
                  << "] TO VIRTUAL_DEVICE = N'" << std::string(device_name.begin(), device_name.end()) << "'\n";
        bool ok = launch_sqlcmd(device_name, backup_db);
        if (!ok) {
            auto_mode = false;
        }
    }

    if (!auto_mode) {
        // Interactive: prompt user to issue the T-SQL command manually
        std::cout << "\n========== INSTRUCTION ==========\n";
        std::cout << "In SQL Server Management Studio, execute:\n\n";
        std::wcout << L"    BACKUP DATABASE [" << backup_db
                   << L"] TO VIRTUAL_DEVICE = N'" << device_name << L"'\n\n";
        std::cout << "Press Enter after issuing the T-SQL command...\n";
        std::cin.get();
    }

    if (!client.open_devices()) {
        std::cerr << "Failed to open devices.\n";
        client.close();
        return 1;
    }

    std::cout << "\nVDI device open and ready. Data transfer starting...\n\n";

    client.process_commands();

    client.close();

    return 0;
}
