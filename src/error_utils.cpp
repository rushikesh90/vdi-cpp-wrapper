#include "error_utils.h"

// ---------------------------------------------------------------------------
// HRESULT to human-readable string translation.
//
// Covers all VDI protocol error codes (0x8077xxxx) plus common COM and
// system error codes that appear in VDI session operations.
// ---------------------------------------------------------------------------

std::string hresult_to_string(unsigned long hr) {
    switch (hr) {
    // ── VDI protocol error codes ──────────────────────────────────────
    case 0x80770001: return "VD_E_CLOSE: Session closed normally by SQL Server";
    case 0x80770002: return "VD_E_OPEN: Failed to open the device";
    case 0x80770003: return "VD_E_TIMEOUT: Operation timed out (SQL Server did not connect)";
    case 0x80770004: return "VD_E_ABORT: Operation aborted";
    case 0x80770005: return "VD_E_BUSY: Resource busy";
    case 0x80770006: return "VD_E_INVALID: Invalid parameter or configuration";
    case 0x80770007: return "VD_E_NOTENOUGH: Insufficient resources";
    case 0x80770008: return "VD_E_ALREADY: Operation already in progress";
    case 0x80770009: return "VD_E_NOTSUPPORTED: Feature or parameter not supported";
    case 0x8077000A: return "VD_E_INVALIDATE: Invalidate request";
    case 0x8077000B: return "VD_E_OPENFAIL: Device open failed";
    case 0x8077000C: return "VD_E_OBJECT: Object not in correct state or wrong call order";
    case 0x8077000D: return "VD_E_INTERRUPT: Operation interrupted";
    case 0x8077000E: return "VD_E_EOF: End of data stream / no more commands";

    // ── COM / system error codes ───────────────────────────────────────
    case 0x80040154: return "REGDB_E_CLASSNOTREG: COM class not registered (CLSID_MSSQL_ClientVirtualDeviceSet not found)";
    case 0x800401F0: return "CO_E_NOTINITIALIZED: CoInitializeEx was not called";
    case 0x80004002: return "E_NOINTERFACE: QueryInterface failed — wrong IID or vtable mismatch";
    case 0x80004001: return "E_NOTIMPL: Function not implemented";
    case 0x80070005: return "E_ACCESSDENIED: Access denied (COM permissions)";
    case 0x80070057: return "E_INVALIDARG: Invalid argument";
    case 0x80004005: return "E_FAIL: Unspecified failure";
    case 0x8000FFFF: return "E_UNEXPECTED: Catastrophic failure";

    // ── Windows error codes commonly seen with VDI ────────────────────
    case 0x8007006E: return "ERROR_OPEN_FAILED: Open operation failed (resource or permission)";
    case 0x80070103: return "ERROR_DEVICE_NOT_CONNECTED: Device not available";
    case 0x8007045A: return "ERROR_IO_DEVICE: I/O device error (SQL Server IO error 995)";

    default:
        // Format unknown codes as hex
        char buf[32];
        snprintf(buf, sizeof(buf), "Unknown HRESULT 0x%08lX", hr);
        return std::string(buf);
    }
}