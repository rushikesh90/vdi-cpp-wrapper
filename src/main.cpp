// VDI Wrapper - Entry point
//
// Usage:
//   1. Run this binary
//   2. In SSMS / sqlcmd, execute:
//        BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'VDI_Device_<pid>'
//   3. Watch throughput and latency metrics
//
// To use a file sink instead of NullSink, change below.

#include "vdi_client.h"
#include "sink.h"       // NullSink
#include "file_sink.h"  // alternative: FileSink("backup_stream.bin")
#include <windows.h>
#include <iostream>
#include <string>

int main() {
    // Generate unique device name using process ID
    DWORD pid = GetCurrentProcessId();
    std::wstring device_name = L"VDI_Device_" + std::to_wstring(pid);
    int device_count = 1;

    // Choose sink: NullSink for benchmarking, FileSink for real backup
    auto sink = std::make_unique<NullSink>();
    // auto sink = std::make_unique<FileSink>("backup_stream.bin");

    VdiClient client(std::move(sink));

    std::cout << "VDI C++ Wrapper\n";
    std::cout << "===============\n";
    std::cout << "Device name: " << std::string(device_name.begin(), device_name.end()) << "\n";
    std::cout << "Device count: " << device_count << "\n\n";

    if (!client.connect(device_name, device_count)) {
        std::cerr << "Failed to connect.\n";
        return 1;
    }

    // Prompt user before blocking on GetConfiguration
    std::cout << "\n========== INSTRUCTION ==========\n";
    std::cout << "In SQL Server Management Studio, execute:\n\n";
    std::wcout << L"    BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'" << device_name << L"'\n\n";
    std::cout << "Press Enter after issuing the T-SQL command...\n";
    std::cin.get();

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