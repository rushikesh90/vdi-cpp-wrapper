#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <vdi.h>
#include <combaseapi.h>
#else
// Non-Windows placeholder definitions to allow compilation on other platforms
typedef void* IClientVirtualDeviceSet2;
typedef int HRESULT;
#define S_OK 0
#define FAILED(hr) ((hr) != S_OK)
#define COINIT_MULTITHREADED 0
inline HRESULT CoInitializeEx(void*, unsigned int) { return S_OK; }
inline void CoUninitialize() {}
inline HRESULT CoCreateInstance(const void*, void*, unsigned int, const void*, void**) { return S_OK; }
#endif
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
    IClientVirtualDeviceSet2* device_set_;
    void* current_cmd_;
};
