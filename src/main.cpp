#include "vdi_client.h"
#include <iostream>
#include <memory>

int main() {
    // Generate a unique device name so multiple runs don't collide
    std::wstring device_name = L"MyVDIDevice_" + std::to_wstring(GetCurrentProcessId());
    int device_count = 1;

    VdiClient client(std::make_unique<NullSink>());

    if (!client.connect(device_name, device_count)) {
        std::cerr << "Failed to connect/create VDI device set.\n";
        return 1;
    }

    // Tell the user what T-SQL to execute before we block on GetConfiguration
    std::cout << "\n========== INSTRUCTION ==========\n";
    std::cout << "In SQL Server Management Studio, execute:\n\n";
    std::wcout << L"    BACKUP DATABASE [tempdb]\n";
    std::wcout << L"    TO VIRTUAL_DEVICE = N'" << device_name << L"'\n\n";
    std::cout << "(The database name and device name must match exactly.)\n\n";
    std::cout << "Press Enter after issuing the T-SQL command...\n";
    std::cin.get();

    if (!client.open_devices()) {
        return 1;
    }

    std::cout << "Devices opened. Entering command processing loop...\n";

    client.process_commands();

    std::cout << "Backup session completed. Press Enter to exit.\n";
    std::cin.get();

    return 0;
}