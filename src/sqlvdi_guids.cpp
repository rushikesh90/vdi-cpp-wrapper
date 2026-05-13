#define INITGUID

#include "sqlvdi_guids.h"

DEFINE_GUID(CLSID_MSSQL_ClientVirtualDeviceSet,
    0x40700425, 0x0080, 0x11d2, 0x85, 0x1f, 0x00, 0xc0, 0x4f, 0xc2, 0x17, 0x59);

// IID_IClientVirtualDeviceSet2 is DIFFERENT from CLSID_MSSQL_ClientVirtualDeviceSet
// Interface UUID is d0e6eb07-7a62-11d2-8573-00c04fc21759
DEFINE_GUID(IID_IClientVirtualDeviceSet2,
    0xd0e6eb07, 0x7a62, 0x11d2, 0x85, 0x73, 0x00, 0xc0, 0x4f, 0xc2, 0x17, 0x59);