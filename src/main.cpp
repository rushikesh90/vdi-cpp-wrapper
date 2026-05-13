#include "backup_engine.h"
#include "backup_request.h"
#include <iostream>

int main() {
    BackupRequest req;
    req.database = L"tempdb";
    req.type = BackupType::Full;
    req.device_name = L"MyVDIDevice_" + std::to_wstring(GetCurrentProcessId());
    req.device_count = 1;

    BackupEngine engine;
    engine.start_backup(req);

    return 0;
}
