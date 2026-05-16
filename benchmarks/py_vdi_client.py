"""
Minimal Python VDI streaming client using ctypes.
Mirrors C++ VdiClient behavior exactly for apples-to-apples benchmarking.

Design:
  - Pure ctypes COM (no pywin32)
  - Direct vtable dispatch for IClientVirtualDeviceSet2 and IClientVirtualDevice
  - NullSink only (orchestration overhead measurement)
  - Metrics matching C++ Metrics class
  - Zero logging during command loop
"""

import ctypes
import ctypes.wintypes
import time
import math
import sys
from ctypes import (
    POINTER, Structure, c_void_p, c_uint8, c_uint16,
    c_uint32, c_uint64,
    c_ulong, c_char_p, c_wchar_p, byref, cast, c_size_t,
    memmove, addressof, sizeof
)

# ---------------------------------------------------------------------------
# Win32 types
# ---------------------------------------------------------------------------
LONG = ctypes.c_long
DWORD = c_uint32
HRESULT = LONG
ULONG_PTR = c_size_t  # pointer-width integer
DWORDLONG = c_uint64
LPCWSTR = c_wchar_p
BOOL = ctypes.c_int

# COM constants
CLSCTX_INPROC_SERVER = 1
COINIT_MULTITHREADED = 0

# INFINITE wait
INFINITE = 0xFFFFFFFF

# ERROR_SUCCESS
ERROR_SUCCESS = 0

# VDI error codes (HRESULT)
VD_E_CLOSE        = 0x80770001
VD_E_OPEN         = 0x80770002
VD_E_TIMEOUT      = 0x80770003
VD_E_ABORT        = 0x80770004
VD_E_INVALID      = 0x80770006
VD_E_NOTSUPPORTED = 0x80770009
VD_E_OBJECT       = 0x8077000c
VD_E_EOF          = 0x8077000e

# VDC command codes
VDC_Read       = 1
VDC_Write      = 2
VDC_ClearError = 3
VDC_Flush      = 12
VDC_Close      = 99  # sentinel (not real VDI)

# VDF defaults
VDF_Default = 0


# ---------------------------------------------------------------------------
# Helper: FAILED / SUCCEEDED macros
# ---------------------------------------------------------------------------
def FAILED(hr):
    return hr < 0

def SUCCEEDED(hr):
    return hr >= 0


# ---------------------------------------------------------------------------
# GUID structure
# ---------------------------------------------------------------------------
class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", c_uint32),
        ("Data2", c_uint16),
        ("Data3", c_uint16),
        ("Data4", c_uint8 * 8),
    ]


# ---------------------------------------------------------------------------
# VDI GUIDs
# ---------------------------------------------------------------------------
CLSID_MSSQL_ClientVirtualDeviceSet = GUID(
    0x40700425, 0x0080, 0x11d2,
    (c_uint8 * 8)(0x85, 0x1f, 0x00, 0xc0, 0x4f, 0xc2, 0x17, 0x59)
)

IID_IClientVirtualDeviceSet2 = GUID(
    0xd0e6eb07, 0x7a62, 0x11d2,
    (c_uint8 * 8)(0x85, 0x73, 0x00, 0xc0, 0x4f, 0xc2, 0x17, 0x59)
)


# ---------------------------------------------------------------------------
# VDConfig structure (packed to 8-byte alignment, 11 fields = 44 bytes)
# ---------------------------------------------------------------------------
class VDConfig(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("deviceCount",       DWORD),
        ("features",          DWORD),
        ("prefixZoneSize",    DWORD),
        ("alignment",         DWORD),
        ("softFileMarkBlockSize", DWORD),
        ("EOMWarningSize",    DWORD),
        ("serverTimeOut",     DWORD),
        ("blockSize",         DWORD),
        ("maxIODepth",        DWORD),
        ("maxTransferSize",   DWORD),
        ("bufferAreaSize",    DWORD),
    ]


# ---------------------------------------------------------------------------
# VDC_Command structure (24 bytes on x64)
# ---------------------------------------------------------------------------
class VDC_Command(ctypes.Structure):
    _fields_ = [
        ("commandCode", DWORD),
        ("size",        DWORD),
        ("position",    DWORDLONG),
        ("buffer",      c_void_p),
    ]


PVOID = c_void_p

# ---------------------------------------------------------------------------
# Win32 API function signatures
# ---------------------------------------------------------------------------
ole32 = ctypes.windll.ole32

# HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit)
ole32.CoInitializeEx.argtypes = [c_void_p, DWORD]
ole32.CoInitializeEx.restype = HRESULT

# void CoUninitialize()
ole32.CoUninitialize.argtypes = []
ole32.CoUninitialize.restype = None

# HRESULT CoCreateInstance(
#     REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext,
#     REFIID riid, LPVOID *ppv
# )
ole32.CoCreateInstance.argtypes = [
    POINTER(GUID), c_void_p, DWORD, POINTER(GUID), POINTER(PVOID)
]
ole32.CoCreateInstance.restype = HRESULT


# ---------------------------------------------------------------------------
# Metrics (mirrors C++ Metrics class)
# ---------------------------------------------------------------------------
class Metrics:
    __slots__ = (
        'total_bytes', 'chunk_count',
        'getcommand_latencies', 'chunk_latencies',
        'start_time'
    )

    def __init__(self):
        self.total_bytes = 0
        self.chunk_count = 0
        self.getcommand_latencies = []   # microseconds
        self.chunk_latencies = []        # microseconds
        self.start_time = time.perf_counter()

    def add_bytes(self, size):
        self.total_bytes += size
        self.chunk_count += 1

    def record_chunk_latency(self, us):
        self.chunk_latencies.append(us)

    def record_getcommand_latency(self, us):
        self.getcommand_latencies.append(us)

    @staticmethod
    def _percentile(values, p):
        """Nearest-rank percentile, matching C++ implementation."""
        if not values:
            return 0
        sorted_vals = sorted(values)
        n = len(sorted_vals)
        rank = math.ceil(p / 100.0 * n)
        idx = max(0, min(rank - 1, n - 1))
        return sorted_vals[idx]

    def print_summary(self):
        """Print metrics identical to C++ Metrics::print_summary()."""
        elapsed = time.perf_counter() - self.start_time
        mb = self.total_bytes / 1024.0 / 1024.0
        throughput = mb / elapsed if elapsed > 0 else 0.0

        print(f"Bytes: {self.total_bytes}")
        print(f"Chunks: {self.chunk_count}")

        if self.chunk_count > 0:
            avg = self.total_bytes // self.chunk_count
            print(f"\nAverage chunk size: {avg} bytes ({avg / 1024.0:.1f} KB)")

        print(f"\nTotal elapsed time: {elapsed:.1f} s")

        # CPU utilization via GetProcessTimes (Win32)
        try:
            kernel_time = c_uint64(0)
            user_time = c_uint64(0)
            kernel_filetime = ctypes.wintypes.FILETIME()
            user_filetime = ctypes.wintypes.FILETIME()
            # We don't actually call GetProcessTimes because the embedded
            # Python may have limited ctypes access. Skip CPU% for Python.
        except Exception:
            pass

        print(f"\nThroughput MB/s: {throughput:.1f}")

        # GetCommand latency
        print("\nGetCommand Latency:")
        if not self.getcommand_latencies:
            print("  (no samples)")
        else:
            print(f"  P50: {self._percentile(self.getcommand_latencies, 50)} us")
            print(f"  P95: {self._percentile(self.getcommand_latencies, 95)} us")
            print(f"  P99: {self._percentile(self.getcommand_latencies, 99)} us")

        # Chunk processing latency
        print("\nChunk Processing:")
        if not self.chunk_latencies:
            print("  (no samples)")
        else:
            print(f"  P50: {self._percentile(self.chunk_latencies, 50)} us")
            print(f"  P95: {self._percentile(self.chunk_latencies, 95)} us")
            print(f"  P99: {self._percentile(self.chunk_latencies, 99)} us")

        print()


# ---------------------------------------------------------------------------
# Python VDI Client (ctypes COM, mirrors C++ VdiClient)
# ---------------------------------------------------------------------------
class PyVdiClient:
    def __init__(self):
        self.metrics = Metrics()
        self.device_set = None   # IClientVirtualDeviceSet2 COM pointer as c_void_p
        self.device = None       # IClientVirtualDevice COM pointer as c_void_p
        self.device_name = ""
        self.device_count = 0

        # Initialize COM
        hr = ole32.CoInitializeEx(None, COINIT_MULTITHREADED)
        if FAILED(hr):
            raise RuntimeError(f"CoInitializeEx failed: hr=0x{hr:08x}")

    def __del__(self):
        self.close()
        try:
            ole32.CoUninitialize()
        except Exception:
            pass

    def _create_device_set(self):
        """CoCreateInstance: obtain IClientVirtualDeviceSet2."""
        ppv = PVOID()
        hr = ole32.CoCreateInstance(
            byref(CLSID_MSSQL_ClientVirtualDeviceSet),
            None,
            CLSCTX_INPROC_SERVER,
            byref(IID_IClientVirtualDeviceSet2),
            byref(ppv)
        )
        if FAILED(hr):
            raise RuntimeError(
                f"CoCreateInstance(CLSID_MSSQL_ClientVirtualDeviceSet) "
                f"failed: hr=0x{hr:08x}"
            )
        self.device_set = ppv

    def _vtable_call(self, iface_ptr, slot, *args):
        """
        Call a virtual method on a COM interface via vtable dispatch.

        slot 0 = QueryInterface
        slot 1 = AddRef
        slot 2 = Release
        slot 3+ = custom methods
        """
        # vtable is at the start of the interface.
        # Read the function pointer from the vtable.
        vtable_ptr = ctypes.c_void_p.from_address(iface_ptr.value)
        # vtable is an array of function pointers
        fn_ptr_raw = ctypes.c_void_p.from_address(
            vtable_ptr.value + slot * ctypes.sizeof(ctypes.c_void_p)
        ).value
        # Create a CFUNCTYPE for the call
        # HRESULT __stdcall func(params)
        func_type = ctypes.WINFUNCTYPE(HRESULT, *[type(a) for a in args])
        func = func_type(fn_ptr_raw)
        return func(*args)

    def connect(self, device_name, device_count):
        """Create device set via CreateEx. Mirrors C++ VdiClient::connect()."""
        self.device_name = device_name
        self.device_count = device_count

        # Phase 1: CoCreateInstance
        self._create_device_set()
        print("VDI device set created successfully.")
        print(f'Device set name: "{self.device_name}"')
        print(f"Device count: {self.device_count}")

        # Phase 2: CreateEx
        config = VDConfig()
        config.deviceCount = device_count
        config.features = VDF_Default
        config.prefixZoneSize = 0
        config.alignment = 0
        config.softFileMarkBlockSize = 0
        config.EOMWarningSize = 0
        config.serverTimeOut = 60000
        config.blockSize = 64 * 1024
        config.maxIODepth = 0
        config.bufferAreaSize = 4 * 1024 * 1024
        config.maxTransferSize = 1024 * 1024

        ds_ptr = self.device_set.value

        # CreateEx vtable slot = 11 (3 IUnknown + 8 IClientVirtualDeviceSet + 0 CreateEx)
        # Signature: HRESULT CreateEx(LPCWSTR instanceName, LPCWSTR name, VDConfig* config)
        # STDMETHODCALLTYPE = __stdcall, so first arg = 'this' (the interface pointer)
        CreateEx_t = ctypes.WINFUNCTYPE(
            HRESULT,
            c_void_p,        # this
            LPCWSTR,           # instanceName
            LPCWSTR,           # name
            POINTER(VDConfig)  # config
        )
        vtable = ctypes.c_void_p.from_address(ds_ptr).value
        fn_createex = ctypes.c_void_p.from_address(
            vtable + 11 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        createex = CreateEx_t(fn_createex)

        hr = createex(ds_ptr, None, device_name, byref(config))
        if FAILED(hr):
            hint = ""
            if hr == VD_E_NOTSUPPORTED:
                hint = " - Hint: VD_E_NOTSUPPORTED - check VDConfig or configuration parameters."
            elif hr == VD_E_INVALID:
                hint = " - Hint: VD_E_INVALID - check VDConfig structure layout."
            raise RuntimeError(f"CreateEx failed: hr=0x{hr:08x}{hint}")

        print("VDI device set created successfully.")

    def open_devices(self):
        """Wait for SQL Server, then open device. Mirrors C++ VdiClient::open_devices()."""
        if self.device_set is None:
            raise RuntimeError("No device set. Call connect() first.")

        ds_ptr = self.device_set.value
        vtable = ctypes.c_void_p.from_address(ds_ptr).value

        # GetConfiguration: vtable slot 4
        #   IUnknown: slots 0-2 (QI, AddRef, Release)
        #   IClientVirtualDeviceSet: slot 3 = Create, slot 4 = GetConfiguration
        # HRESULT GetConfiguration(DWORD timeout, VDConfig* config)
        GetConfiguration_t = ctypes.WINFUNCTYPE(
            HRESULT, c_void_p, DWORD, POINTER(VDConfig)
        )
        fn_gc = ctypes.c_void_p.from_address(
            vtable + 4 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        get_config = GetConfiguration_t(fn_gc)

        # Phase 1: GetConfiguration (blocks for SQL Server)
        print(f'Waiting for SQL Server to connect to device set "{self.device_name}"...')
        print(f"(Execute: BACKUP DATABASE [db] TO VIRTUAL_DEVICE = N'{self.device_name}')")

        server_config = VDConfig()
        hr = get_config(ds_ptr, 60000, byref(server_config))
        if FAILED(hr):
            hint = ""
            if hr == VD_E_TIMEOUT:
                hint = " - Hint: VD_E_TIMEOUT - SQL Server did not connect. Run the T-SQL BACKUP command."
            elif hr == VD_E_OBJECT:
                hint = " - Hint: VD_E_OBJECT - device set not ready."
            raise RuntimeError(f"GetConfiguration failed: hr=0x{hr:08x}{hint}")

        print("SQL Server connected. Server configuration:")
        print(f"  deviceCount: {server_config.deviceCount}")
        print(f"  features: 0x{server_config.features:08x}")
        print(f"  serverTimeOut: {server_config.serverTimeOut}")
        print(f"  blockSize: {server_config.blockSize}")
        print(f"  maxTransferSize: {server_config.maxTransferSize}")

        # Phase 2: OpenDevice - vtable slot 5
        #   IUnknown: slots 0-2
        #   IClientVirtualDeviceSet: slot 3 = Create, slot 4 = GetConfiguration,
        #                            slot 5 = OpenDevice
        # HRESULT OpenDevice(LPCWSTR name, IClientVirtualDevice** ppDevice)
        OpenDevice_t = ctypes.WINFUNCTYPE(
            HRESULT, c_void_p, LPCWSTR, POINTER(PVOID)
        )
        fn_od = ctypes.c_void_p.from_address(
            vtable + 5 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        open_dev = OpenDevice_t(fn_od)

        pp_dev = PVOID()
        hr = open_dev(ds_ptr, self.device_name, byref(pp_dev))
        if FAILED(hr):
            raise RuntimeError(f"OpenDevice failed: hr=0x{hr:08x}")

        self.device = pp_dev
        print(f'Opened virtual device: "{self.device_name}"')

    def process_commands(self):
        """Command processing loop. Mirrors C++ VdiClient::process_commands()."""
        if self.device is None:
            raise RuntimeError("No device. Call open_devices() first.")

        dev_ptr = self.device.value
        vtable = ctypes.c_void_p.from_address(dev_ptr).value

        # GetCommand: vtable slot 3 (3 IUnknown = slot 3)
        # HRESULT GetCommand(DWORD timeout, VDC_Command** ppCmd)
        GetCommand_t = ctypes.WINFUNCTYPE(
            HRESULT, c_void_p, DWORD, POINTER(POINTER(VDC_Command))
        )
        fn_gc = ctypes.c_void_p.from_address(
            vtable + 3 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        get_cmd = GetCommand_t(fn_gc)

        # CompleteCommand: vtable slot 4
        # HRESULT CompleteCommand(VDC_Command* cmd, DWORD status, DWORD bytes, DWORDLONG position)
        CompleteCommand_t = ctypes.WINFUNCTYPE(
            HRESULT, c_void_p, POINTER(VDC_Command), DWORD, DWORD, DWORDLONG
        )
        fn_cc = ctypes.c_void_p.from_address(
            vtable + 4 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        complete_cmd = CompleteCommand_t(fn_cc)

        running = True
        pp_cmd = POINTER(VDC_Command)()

        while running:
            cmd_start = time.perf_counter()

            hr = get_cmd(dev_ptr, INFINITE, byref(pp_cmd))

            cmd_end = time.perf_counter()

            if FAILED(hr):
                if hr == VD_E_CLOSE:
                    print("GetCommand returned VD_E_CLOSE - backup complete.")
                elif hr == VD_E_EOF:
                    print("GetCommand returned VD_E_EOF - backup complete.")
                else:
                    print(f"GetCommand failed: hr=0x{hr:08x}")
                break

            # Record GetCommand latency in microseconds
            latency_us = int((cmd_end - cmd_start) * 1_000_000)
            self.metrics.record_getcommand_latency(latency_us)

            if not pp_cmd:
                continue

            cmd = pp_cmd.contents

            # NO logging during benchmark (comment out if debugging needed)
            # print(f"Received command: code={cmd.commandCode}")

            # Handle commands (same dispatch as C++)
            hr2 = 0  # S_OK
            if cmd.commandCode == VDC_Write:
                self._process_write(cmd)
            elif cmd.commandCode == VDC_Flush:
                # NullSink: no flush needed
                pass
            elif cmd.commandCode == VDC_Close:
                print("  [VDC_Close]")
                running = False
                continue
            else:
                # Unknown command - just complete
                pass

            # CompleteCommand: 4 args (cmd, ERROR_SUCCESS, size, position)
            cc_hr = complete_cmd(dev_ptr, pp_cmd, ERROR_SUCCESS, cmd.size, cmd.position)
            if FAILED(cc_hr):
                print(f"CompleteCommand failed: hr=0x{cc_hr:08x}")

        # Print summary at end (matches C++ behavior)
        self.metrics.print_summary()

    def _process_write(self, cmd):
        """Process a VDC_Write command. NullSink: no actual I/O."""
        start = time.perf_counter()
        self.metrics.add_bytes(cmd.size)
        # NullSink: data is ignored, just track timing
        end = time.perf_counter()
        latency_us = int((end - start) * 1_000_000)
        self.metrics.record_chunk_latency(latency_us)

    def close(self):
        """Release resources."""
        if self.device is not None:
            # Release via vtable slot 2
            try:
                dev_ptr = self.device.value
                vtable = ctypes.c_void_p.from_address(dev_ptr).value
                Release_t = ctypes.WINFUNCTYPE(HRESULT, c_void_p)
                fn_rel = ctypes.c_void_p.from_address(
                    vtable + 2 * ctypes.sizeof(ctypes.c_void_p)
                ).value
                release = Release_t(fn_rel)
                release(dev_ptr)
            except Exception:
                pass
            self.device = None

        if self.device_set is not None:
            # Close via vtable slot 6
            #   IUnknown: slots 0-2
            #   IClientVirtualDeviceSet: slot 3 = Create, slot 4 = GetConfiguration,
            #                            slot 5 = OpenDevice, slot 6 = Close
            try:
                ds_ptr = self.device_set.value
                vtable = ctypes.c_void_p.from_address(ds_ptr).value

                # Close: slot 6
                Close_t = ctypes.WINFUNCTYPE(HRESULT, c_void_p)
                fn_close = ctypes.c_void_p.from_address(
                    vtable + 6 * ctypes.sizeof(ctypes.c_void_p)
                ).value
                close = Close_t(fn_close)
                close(ds_ptr)

                # Release: slot 2
                Release_t = ctypes.WINFUNCTYPE(HRESULT, c_void_p)
                fn_rel = ctypes.c_void_p.from_address(
                    vtable + 2 * ctypes.sizeof(ctypes.c_void_p)
                ).value
                release = Release_t(fn_rel)
                release(ds_ptr)
            except Exception:
                pass
            self.device_set = None