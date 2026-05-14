#include "backup_engine.h"
#include "vdi_client.h"
#include "file_sink.h"
#include <iostream>

BackupEngine::BackupEngine() {}

void BackupEngine::start_backup(const BackupRequest& request) {
    // Phase 1: Create the VDI device set

    auto client = std::make_unique<VdiClient>(std::make_unique<FileSink>("backup_stream.bin"));
    if (!client->connect(request.device_name, request.device_count)) {
        return;
    }

    // Phase 2: Tell the user exactly what T-SQL to execute, then wait for
    // them to confirm before calling GetConfiguration (which blocks until
    // SQL Server connects).
    std::cout << "\n========== INSTRUCTION ==========\n";
    std::cout << "In SQL Server Management Studio, execute:\n\n";
    std::wcout << L"    BACKUP DATABASE [" << request.database << L"]\n";
    std::wcout << L"    TO VIRTUAL_DEVICE = N'" << request.device_name << L"'\n\n";
    std::cout << "(The database name and device name must match exactly.)\n\n";
    std::cout << "Press Enter after issuing the T-SQL command...\n";
    std::cin.get();

    if (!client->open_devices()) {
        return;
    }

    std::cout << "VDI device open and ready. Data transfer starting...\n";

    client->process_commands();

    std::cout << "Backup session completed. Press Enter to exit.\n";
    std::cin.get();
}
