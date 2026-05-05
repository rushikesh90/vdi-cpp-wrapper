#include "vdi_client.h"

VdiClient::VdiClient() : device_set_(nullptr), current_cmd_(nullptr) {}

VdiClient::~VdiClient() {}

bool VdiClient::connect(const std::wstring&, int) {
    return true;
}

bool VdiClient::get_next_chunk(uint8_t*& data, size_t& size) {
    data = nullptr;
    size = 0;
    return false;
}

void VdiClient::complete_chunk() {}

void VdiClient::close() {}