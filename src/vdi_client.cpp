#include "vdi_client.h"

VdiClient::VdiClient() : device_set_(nullptr), current_cmd_(nullptr) {}

VdiClient::~VdiClient() {
#if defined(_WIN32)
    if (device_set_) {
        device_set_->Release();
        device_set_ = nullptr;
    }
    CoUninitialize();
#endif
}

bool VdiClient::connect(const std::wstring& device_name, int device_count) {
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(
        CLSID_MSSQL_ClientVirtualDeviceSet,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IClientVirtualDeviceSet2,
        (void**)&device_set_
    );

    if (FAILED(hr)) return false;

    VDConfig config = {};
    config.deviceCount = device_count;
    config.features = VDF_Default;
    config.commandTimeout = 60000;

    hr = device_set_->CreateEx(device_name.c_str(), &config);

    return SUCCEEDED(hr);
#else
    // Non-Windows placeholder - always succeeds
    return true;
#endif
}

bool VdiClient::get_next_chunk(uint8_t*& data, size_t& size) {
    data = nullptr;
    size = 0;
    return false;
}

void VdiClient::complete_chunk() {}

void VdiClient::close() {}</arg_value>
<arg_key>task_progress</arg_key>
<arg_value>- [x] Update header file with new includes and device_set_ type
- [x] Implement connect() with COM initialization
- [ ] Update destructor to release device_set_ and uninitialize COM (done in connect update)
- [ ] Update CMakeLists.txt to link ole32.lib
- [ ] Update main.cpp to test the implementation
- [ ] Commit changes to git</arg_value>
