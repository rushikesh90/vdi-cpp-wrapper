#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class VdiClient {
public:
    VdiClient();
    ~VdiClient();

    bool connect(const std::wstring& device_name, int device_count);
    bool get_next_chunk(uint8_t*& data, size_t& size);
    void complete_chunk();
    void close();

private:
    void* device_set_;
    void* current_cmd_;
};
