#include "backup_engine.h"
#include <windows.h>
#include <iostream>

int main() {
    BackupRequest request;
    request.database = L"tempdb";
    request.type = BackupType::Full;
    request.device_count = 1;
    request.device_name = L"VDI_Device_" + std::to_wstring(GetCurrentProcessId());

    BackupEngine engine;
    engine.start_backup(request);

    return 0;
}
