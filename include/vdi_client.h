#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <sqlvdi.h>
#include <combaseapi.h>
#else
// Non-Windows placeholder definitions to allow compilation on other platforms
typedef void* IClientVirtualDeviceSet2;
typedef void* IClientVirtualDevice;
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
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <chrono>

#include "sink.h"
#include "metrics.h"
#include "buffer_pool.h"

// Configuration constants
constexpr size_t BUFFER_POOL_SIZE = 64;
constexpr size_t BUFFER_SIZE = 64 * 1024;

class VdiClient {
public:
    VdiClient(std::unique_ptr<Sink> sink);
    ~VdiClient();

    bool connect(const std::wstring& device_name, int device_count);
    bool open_devices();
    bool get_next_chunk(uint8_t*& data, size_t& size);
    void complete_chunk();
    void close();

    // Command processing loop
    void process_commands();

private:
    void handle_command(IClientVirtualDevice* device,
                        VDC_Command* cmd);

    void process_write(
        VDC_Command* writeCmd);

    IClientVirtualDeviceSet2* device_set_;
    void* current_cmd_;

    std::unique_ptr<Sink> sink_;
    Metrics metrics_;
    BufferPool buffer_pool_{BUFFER_SIZE, BUFFER_POOL_SIZE};

    std::vector<IClientVirtualDevice*> devices_;
    std::wstring device_name_;
    int device_count_;
};
