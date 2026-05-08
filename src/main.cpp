#include "backup_engine.h"
#include <cstdio>

int main() {
    BackupRequest req;
    req.database = L"testdb";
    req.type = BackupType::Full;

    BackupEngine engine;
    engine.start_backup(req);

    return 0;
}
