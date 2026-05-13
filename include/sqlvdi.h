#ifndef SQLVDI_H
#define SQLVDI_H

#include <windows.h>
#include <unknwn.h>

#include "sqlvdi_guids.h"

// Virtual Device Configuration Structure (packed per MIDL spec)
#pragma pack(push, 8)
typedef struct _VDConfig {
    DWORD deviceCount;
    DWORD features;
    DWORD prefixZoneSize;
    DWORD alignment;
    DWORD softFileMarkBlockSize;
    DWORD EOMWarningSize;
    DWORD serverTimeOut;
    DWORD blockSize;
    DWORD maxIODepth;
    DWORD maxTransferSize;
    DWORD bufferAreaSize;
} VDConfig, *PVDConfig;
#pragma pack(pop)

// Feature flags
#define VDF_Removable       0x1
#define VDF_Rewind          0x2
#define VDF_Position        0x10
#define VDF_SkipBlocks      0x20
#define VDF_ReversePosition 0x40
#define VDF_Discard         0x80
#define VDF_FileMarks       0x100
#define VDF_RandomAccess    0x200
#define VDF_SnapshotPrepare 0x400
#define VDF_EnumFrozenFiles 0x800
#define VDF_VSSWriter       0x1000
#define VDF_WriteMedia      0x10000
#define VDF_ReadMedia       0x20000
#define VDF_LatchStats      0x80000000
#define VDF_LikePipe        0
#define VDF_LikeTape        (VDF_FileMarks | VDF_Removable | VDF_Rewind | VDF_Position | VDF_SkipBlocks | VDF_ReversePosition)
#define VDF_LikeDisk        VDF_RandomAccess

// Default features for basic backup
#define VDF_Default         0x00000000

// Command codes
enum VDCommands {
    VDC_Read            = 1,
    VDC_Write           = 2,
    VDC_ClearError      = 3,
    VDC_Rewind          = 4,
    VDC_WriteMark       = 5,
    VDC_SkipMarks       = 6,
    VDC_SkipBlocks      = 7,
    VDC_Load            = 8,
    VDC_GetPosition     = 9,
    VDC_SetPosition     = 10,
    VDC_Discard         = 11,
    VDC_Flush           = 12,
    VDC_Snapshot        = 13,
    VDC_MountSnapshot   = 14,
    VDC_PrepareToFreeze = 15,
    VDC_FileInfoBegin   = 16,
    VDC_FileInfoEnd     = 17
};

typedef struct _VDC_Command {
    DWORD commandCode;
    DWORD size;
    DWORDLONG position;
    BYTE *buffer;
} VDC_Command, *PVDC_Command;

// Forward declarations
interface IClientVirtualDevice;

// IClientVirtualDevice interface
// IID: 40700424-0080-11d2-851f-00c04fc21759
interface IClientVirtualDevice : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCommand(
        /* [in] */ DWORD dwTimeOut,
        /* [out] */ VDC_Command **ppCmd) = 0;

    virtual HRESULT STDMETHODCALLTYPE CompleteCommand(
        /* [in] */ VDC_Command *pCmd,
        /* [in] */ DWORD dwCompletionCode,
        /* [in] */ DWORD dwBytesTransferred,
        /* [in] */ DWORDLONG dwlPosition) = 0;
};

// IClientVirtualDeviceSet interface
// IID: 40700425-0080-11d2-851f-00c04fc21759
interface IClientVirtualDeviceSet : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Create(
        /* [in] */ LPCWSTR lpName,
        /* [in] */ VDConfig *pCfg) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetConfiguration(
        /* [in] */ DWORD dwTimeOut,
        /* [out] */ VDConfig *pCfg) = 0;

    virtual HRESULT STDMETHODCALLTYPE OpenDevice(
        /* [in] */ LPCWSTR lpName,
        /* [out] */ IClientVirtualDevice **ppVirtualDevice) = 0;

    virtual HRESULT STDMETHODCALLTYPE Close(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE SignalAbort(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE OpenInSecondary(
        /* [in] */ LPCWSTR lpSetName) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetBufferHandle(
        /* [in] */ BYTE *pBuffer,
        /* [out] */ DWORD *pBufferHandle) = 0;

    virtual HRESULT STDMETHODCALLTYPE MapBufferHandle(
        /* [in] */ DWORD dwBuffer,
        /* [out] */ BYTE **ppBuffer) = 0;
};

// IClientVirtualDeviceSet2 interface
// IID: d0e6eb07-7a62-11d2-8573-00c04fc21759
// Inherits from IClientVirtualDeviceSet (not directly from IUnknown)
interface IClientVirtualDeviceSet2 : public IClientVirtualDeviceSet {
    virtual HRESULT STDMETHODCALLTYPE CreateEx(
        /* [in] */ LPCWSTR lpInstanceName,
        /* [in] */ LPCWSTR lpName,
        /* [in] */ VDConfig *pCfg) = 0;

    virtual HRESULT STDMETHODCALLTYPE OpenInSecondaryEx(
        /* [in] */ LPCWSTR lpInstanceName,
        /* [in] */ LPCWSTR lpSetName) = 0;
};

#endif // SQLVDI_H