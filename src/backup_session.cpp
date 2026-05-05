#include "backup_session.h"

BackupSession::BackupSession(std::unique_ptr<VdiClient> client,
                             std::unique_ptr<Sink> sink)
    : client_(std::move(client)), sink_(std::move(sink)) {}

void BackupSession::run() {
    uint8_t* data = nullptr;
    size_t size = 0;

    while (client_->get_next_chunk(data, size)) {
        sink_->write(data, size);
        client_->complete_chunk();
    }
}