"""
Minimal Python VDI streaming client using ctypes.
Mirrors C++ VdiClient behavior exactly for apples-to-apples benchmarking.

Design:
  - Pure ctypes COM (no pywin32)
  - Direct vtable dispatch for IClientVirtualDeviceSet2 and IClientVirtualDevice
  - NullSink only (orchestration overhead measurement)
  - Metrics matching C++ Metrics class (including chunk size, histogram, buffer pool)
  - CPU utilization via GetProcessTimes
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
# GetProcessTimes wrapper
# ---------------------------------------------------------------------------
kernel32 = ctypes.windll.kernel32

# BOOL GetProcessTimes(HANDLE, FILETIME*, FILETIME*, FILETIME*, FILETIME*)
class FILETIME(ctypes.Structure):
    _fields_ = [
        ("dwLowDateTime",  DWORD),
        ("dwHighDateTime", DWORD),
    ]

kernel32.GetProcessTimes.argtypes = [
    c_void_p, POINTER(FILETIME), POINTER(FILETIME),
    POINTER(FILETIME), POINTER(FILETIME)
]
kernel32.GetProcessTimes.restype = BOOL

# HANDLE GetCurrentProcess()
kernel32.GetCurrentProcess.argtypes = []
kernel32.GetCurrentProcess.restype = c_void_p


def filetime_to_us(ft):
    """Convert FILETIME to microseconds."""
    ui = (ctypes.c_uint64(ft.dwHighDateTime) << 32) | ctypes.c_uint64(ft.dwLowDateTime)
    return ui.value // 10


# ---------------------------------------------------------------------------
# Metrics (mirrors C++ Metrics class with all enhancements)
# ---------------------------------------------------------------------------
class Metrics:
    __slots__ = (
        'total_bytes', 'chunk_count',
        'min_chunk_size', 'max_chunk_size', 'sum_chunk_sizes',
        'hist_under_100us', 'hist_100us_1ms', 'hist_1ms_10ms', 'hist_over_10ms',
        'buffer_acquire_count', 'buffer_release_count',
        'buffer_pool_hits', 'buffer_pool_misses',
        'getcommand_latencies', 'chunk_latencies',
        'start_time', 'logging_enabled'
    )

    def __init__(self, enable_logging=False):
        self.logging_enabled = enable_logging
        self.total_bytes = 0
        self.chunk_count = 0
        self.min_chunk_size = 2**64 - 1  # UINT64_MAX sentinel
        self.max_chunk_size = 0
        self.sum_chunk_sizes = 0
        self.hist_under_100us = 0
        self.hist_100us_1ms = 0
        self.hist_1ms_10ms = 0
        self.hist_over_10ms = 0
        self.buffer_acquire_count = 0
        self.buffer_release_count = 0
        self.buffer_pool_hits = 0
        self.buffer_pool_misses = 0
        self.getcommand_latencies = []   # microseconds
        self.chunk_latencies = []        # microseconds
        self.start_time = time.perf_counter()

    def add_bytes(self, size):
        self.total_bytes += size
        self.chunk_count += 1

    def record_chunk_size(self, size):
        if size < self.min_chunk_size:
            self.min_chunk_size = size
        if size > self.max_chunk_size:
            self.max_chunk_size = size
        self.sum_chunk_sizes += size

    def record_chunk_latency(self, us):
        self.chunk_latencies.append(us)

    def record_getcommand_latency(self, us):
        self.getcommand_latencies.append(us)

    def record_latency_histogram(self, us):
        if us < 100:
            self.hist_under_100us += 1
        elif us < 1000:
            self.hist_100us_1ms += 1
        elif us < 10000:
            self.hist_1ms_10ms += 1
        else:
            self.hist_over_10ms += 1

    def record_buffer_acquire(self, success):
        self.buffer_acquire_count += 1
        if success:
            self.buffer_pool_hits += 1
        else:
            self.buffer_pool_misses += 1

    def record_buffer_release(self):
        self.buffer_release_count += 1

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

        # --- Aggregate stats ---
        print(f"Bytes: {self.total_bytes}")
        print(f"Chunks: {self.chunk_count}")

        # --- Chunk size stats ---
        print("\nChunk Sizes:")
        if self.chunk_count > 0:
            cur_min = self.min_chunk_size if self.min_chunk_size != 2**64 - 1 else 0
            cur_max = self.max_chunk_size
            avg = self.sum_chunk_sizes // self.chunk_count if self.chunk_count > 0 else 0
            print(f"  Min: {cur_min} bytes ({cur_min / 1024.0:.1f} KB)")
            print(f"  Avg: {avg} bytes ({avg / 1024.0:.1f} KB)")
            print(f"  Max: {cur_max} bytes ({cur_max / 1024.0:.1f} KB)")
        else:
            print("  (no data)")

        if self.chunk_count > 0:
            avg = self.total_bytes // self.chunk_count
            print(f"\nAverage chunk size (from totals): {avg} bytes ({avg / 1024.0:.1f} KB)")

        # Total elapsed time
        print(f"\nTotal elapsed time: {elapsed:.1f} s")

        # CPU utilization via GetProcessTimes
        try:
            creation_time = FILETIME()
            exit_time = FILETIME()
            kernel_time = FILETIME()
            user_time = FILETIME()
            hproc = kernel32.GetCurrentProcess()
            ret = kernel32.GetProcessTimes(
                hproc, byref(creation_time), byref(exit_time),
                byref(kernel_time), byref(user_time)
            )
            if ret:
                kernel_us = filetime_to_us(kernel_time)
                user_us = filetime_to_us(user_time)
                total_cpu_us = kernel_us + user_us
                wall_us = elapsed * 1_000_000.0
                cpu_pct = (total_cpu_us / wall_us) * 100.0 if wall_us > 0 else 0.0
                print(f"CPU utilization: {cpu_pct:.1f}%"
                      f" (kernel={kernel_us // 1000} ms, user={user_us // 1000} ms,"
                      f" total={total_cpu_us // 1000} ms)")
        except Exception:
            pass

        # Throughput
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

        # Latency histogram
        total_samples = (self.hist_under_100us + self.hist_100us_1ms +
                         self.hist_1ms_10ms + self.hist_over_10ms)
        print("\nLatency Histogram (chunk processing):")
        if total_samples == 0:
            print("  (no samples)")
        else:
            def pct(count):
                return (count / total_samples) * 100.0
            print(f"  <100 us       {self.hist_under_100us:>8}  {pct(self.hist_under_100us):.1f}%")
            print(f"  100 us-1 ms   {self.hist_100us_1ms:>8}  {pct(self.hist_100us_1ms):.1f}%")
            print(f"  1 ms-10 ms    {self.hist_1ms_10ms:>8}  {pct(self.hist_1ms_10ms):.1f}%")
            print(f"  >10 ms        {self.hist_over_10ms:>8}  {pct(self.hist_over_10ms):.1f}%")

        # Buffer pool utilization
        acquires = self.buffer_acquire_count
        releases = self.buffer_release_count
        hits = self.buffer_pool_hits
        misses = self.buffer_pool_misses
        reuse_rate = (hits / acquires) * 100.0 if acquires > 0 else 0.0
        print(f"\nBuffer Pool:")
        print(f"  Acquires:      {acquires}")
        print(f"  Releases:      {releases}")
        print(f"  Hits:          {hits}")
        print(f"  Misses:        {misses}")
        print(f"  Reuse rate:    {reuse_rate:.1f}%")

        # Logging status
        if self.logging_enabled:
            print("\n* Logging was ON during this run")

        print()


# ---------------------------------------------------------------------------
# Python VDI Client (ctypes COM, mirrors C++ VdiClient)
# ---------------------------------------------------------------------------
class PyVdiClient:
    def __init__(self, enable_logging=False):
        self.logging_enabled = enable_logging
        self.metrics = Metrics(enable_logging)
        self.device_set = None   # IClientVirtualDeviceSet2 COM pointer as c_void_p
        self.device = None       # IClientVirtualDevice COM pointer as c_void_p
        self.device_name = ""
        self.device_count = 0

        # Simulated buffer pool (for allocation accounting)
        self._pool_buffers = []
        self._pool_available = []
        self._POOL_SIZE = 64
        self._BUFFER_SIZE = 64 * 1024
        self._init_buffer_pool()

        # Initialize COM
        hr = ole32.CoInitializeEx(None, COINIT_MULTITHREADED)
        if FAILED(hr):
            raise RuntimeError(f"CoInitializeEx failed: hr=0x{hr:08x}")

    def _init_buffer_pool(self):
        """Pre-allocate pool buffers (mirrors C++ BufferPool)."""
        self._pool_buffers = [
            bytearray(self._BUFFER_SIZE) for _ in range(self._POOL_SIZE)
        ]
        # Wrap in ctypes for acquire/release simulation
        self._pool_available = list(range(self._POOL_SIZE))

    def _pool_acquire(self):
        """Simulate BufferPool::acquire(). Returns pointer or None."""
        if self._pool_available:
            idx = self._pool_available.pop(0)
            buf = self._pool_buffers[idx]
            # Return a ctypes pointer to the buffer's underlying memory
            return (ctypes.c_uint8 * self._BUFFER_SIZE).from_buffer(buf)
        return None

    def _pool_release(self, buf):
        """Simulate BufferPool::release()."""
        # Find which slot this buffer belongs to (by identity)
        for idx, b in enumerate(self._pool_buffers):
            if ctypes.addressof(buf) == ctypes.addressof(
                    (ctypes.c_uint8 * self._BUFFER_SIZE).from_buffer(b)):
                self._pool_available.append(idx)
                return

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

        # GetCommand: vtable slot 3
        GetCommand_t = ctypes.WINFUNCTYPE(
            HRESULT, c_void_p, DWORD, POINTER(POINTER(VDC_Command))
        )
        fn_gc = ctypes.c_void_p.from_address(
            vtable + 3 * ctypes.sizeof(ctypes.c_void_p)
        ).value
        get_cmd = GetCommand_t(fn_gc)

        # CompleteCommand: vtable slot 4
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

            if self.logging_enabled:
                print(f"Received command: code={cmd.commandCode}")

            # Handle commands (same dispatch as C++)
            hr2 = 0  # S_OK
            if cmd.commandCode == VDC_Write:
                self._process_write(cmd)
            elif cmd.commandCode == VDC_Flush:
                if self.logging_enabled:
                    print("  [VDC_Flush]")
                # NullSink: no flush needed
            elif cmd.commandCode == VDC_Close:
                print("  [VDC_Close]")
                running = False
                continue
            else:
                if self.logging_enabled:
                    print(f"  [UNKNOWN COMMAND: {cmd.commandCode}]")

            # CompleteCommand: 4 args (cmd, ERROR_SUCCESS, size, position)
            cc_hr = complete_cmd(dev_ptr, pp_cmd, ERROR_SUCCESS, cmd.size, cmd.position)
            if FAILED(cc_hr):
                print(f"CompleteCommand failed: hr=0x{cc_hr:08x}")

        # Print summary at end (matches C++ behavior)
        self.metrics.print_summary()

    def _process_write(self, cmd):
        """Process a VDC_Write command. NullSink: no actual I/O."""
        start = time.perf_counter()

        # Record chunk size metrics (min/max/avg)
        self.metrics.record_chunk_size(cmd.size)

        self.metrics.add_bytes(cmd.size)

        if self.logging_enabled:
            print(f"[WRITE] size={cmd.size}")

        # Buffer pool instrumentation: simulate acquire/release
        pool_buf = self._pool_acquire()
        self.metrics.record_buffer_acquire(pool_buf is not None)
        if pool_buf is not None:
            self._pool_release(pool_buf)
        self.metrics.record_buffer_release()

        # NullSink: data is ignored, just track timing
        end = time.perf_counter()
        latency_us = int((end - start) * 1_000_000)

        self.metrics.record_chunk_latency(latency_us)
        self.metrics.record_latency_histogram(latency_us)

    def close(self):
        """Release resources."""
        if self.device is not None:
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
            try:
                ds_ptr = self.device_set.value
                vtable = ctypes.c_void_p.from_address(ds_ptr).value

                Close_t = ctypes.WINFUNCTYPE(HRESULT, c_void_p)
                fn_close = ctypes.c_void_p.from_address(
                    vtable + 6 * ctypes.sizeof(ctypes.c_void_p)
                ).value
                close = Close_t(fn_close)
                close(ds_ptr)

                Release_t = ctypes.WINFUNCTYPE(HRESULT, c_void_p)
                fn_rel = ctypes.c_void_p.from_address(
                    vtable + 2 * ctypes.sizeof(ctypes.c_void_p)
                ).value
                release = Release_t(fn_rel)
                release(ds_ptr)
            except Exception:
                pass
            self.device_set = None
