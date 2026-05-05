#pragma once
#include <string>

enum class BackupType {
    Full,
    Differential,
    Log
};

struct BackupRequest {
    std::wstring database;
    BackupType type{BackupType::Full};

    int device_count{1};
    size_t buffer_size{64 * 1024}; // 64KB default

    std::wstring device_name{L"VDI_Device"};
};
