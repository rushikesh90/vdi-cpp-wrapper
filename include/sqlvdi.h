#ifndef SQLVDI_H
#define SQLVDI_H

#include <windows.h>
#include <unknwn.h>

#include "sqlvdi_guids.h"

// Virtual Device Configuration Structure
typedef struct _VDConfig {
    DWORD deviceCount;
    DWORD features;
    DWORD commandTimeout;
} VDConfig, *PVDConfig;

// Feature flags
#define VDF_Default 0x00000000

// IClientVirtualDeviceSet2 interface (forward declaration)
interface IClientVirtualDeviceSet2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateEx(LPCWSTR lpName, VDConfig* pConfig) = 0;
    // Other methods would be defined here, but we only use CreateEx
};

#endif // SQLVDI_H