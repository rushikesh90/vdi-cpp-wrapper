#pragma once
#include <memory>
#include "vdi_client.h"
#include "sink.h"

class BackupSession {
public:
    BackupSession(std::unique_ptr<VdiClient> client,
                  std::unique_ptr<Sink> sink);

    void run();

private:
    std::unique_ptr<VdiClient> client_;
    std::unique_ptr<Sink> sink_;
};