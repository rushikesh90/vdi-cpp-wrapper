#include "com_runtime.h"
#include "logging.h"

#if defined(_WIN32)
#include <windows.h>
#include <combaseapi.h>
#endif

ComRuntime::ComRuntime()
    : initialized_(false)
    , init_result_(0)
    , already_init_(false)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(
        NULL,
        COINIT_MULTITHREADED);

    init_result_ = static_cast<unsigned long>(hr);

    if (SUCCEEDED(hr)) {
        initialized_ = true;
        already_init_ = (hr == S_FALSE);
        if (already_init_) {
            LOG_WARN("COM was already initialized on this thread (S_FALSE).");
        }
    } else {
        LOG_ERROR("CoInitializeEx failed: HRESULT=0x%08lx", init_result_);
    }
#else
    // Non-Windows: COM is a no-op
    initialized_ = true;
    init_result_ = 0;
    already_init_ = false;
#endif
}

ComRuntime::~ComRuntime() {
#if defined(_WIN32)
    if (initialized_ && !already_init_) {
        CoUninitialize();
    }
#endif
}