#pragma once
#include "backup_request.h"
#include "backup_session.h"
#include <memory>

class BackupEngine {
public:
    BackupEngine();

    // Entry point for external systems
    void start_backup(const BackupRequest& request);

private:
    std::unique_ptr<BackupSession> session_;
};
