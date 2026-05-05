#include "backup_engine.h"
#include "vdi_client.h"
#include "sink.h"

BackupEngine::BackupEngine() {}

void BackupEngine::start_backup(const BackupRequest& request) {
    // Phase 1: ignore SQL trigger, just wire pipeline

    auto client = std::make_unique<VdiClient>();
    client->connect(request.device_name, request.device_count);

    auto sink = std::make_unique<NullSink>();

    session_ = std::make_unique<BackupSession>(
        std::move(client),
        std::move(sink)
    );

    session_->run();
}