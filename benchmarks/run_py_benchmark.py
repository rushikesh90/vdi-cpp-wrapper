"""
Python VDI Benchmark Entry Point (NullSink mode).
Mirrors C++ VdiClient behavior exactly for apples-to-apples comparison.

Usage:
    python run_py_benchmark.py [device_name]

If device_name is omitted, generates one like: VDI_PyBench_<pid>
"""

import sys
import os
import ctypes

# Add this script's directory to sys.path so py_vdi_client can be found
# regardless of the CWD from which the script is launched
_script_dir = os.path.dirname(os.path.abspath(__file__))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

# Add the embedded Python's standard library modules path
# (needed since we're using the embedded distribution)
_embed_dir = os.path.join(_script_dir, 'python-3.13.3-embed-amd64')
if _embed_dir not in sys.path:
    sys.path.insert(0, _embed_dir)

from py_vdi_client import PyVdiClient

def main():
    # Generate device name same pattern as C++: VDI_Device_<pid>
    pid = os.getpid()
    if len(sys.argv) > 1:
        device_name = sys.argv[1]
    else:
        device_name = f"VDI_PyBench_{pid}"

    device_count = 1

    print("=" * 60)
    print("Python VDI Benchmark (NullSink mode)")
    print("=" * 60)
    print(f"Device name: {device_name}")
    print(f"Device count: {device_count}")
    print()

    client = None
    try:
        client = PyVdiClient()

        # Phase 1: Create device set
        client.connect(device_name, device_count)

        # Phase 2: Wait for user to issue T-SQL
        print("\n========== INSTRUCTION ==========")
        print("In SQL Server Management Studio, execute:\n")
        print(f"    BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'{device_name}'\n")
        print("(The database name and device name must match exactly.)\n")
        input("Press Enter after issuing the T-SQL command...\n")

        # Phase 3: GetConfiguration + OpenDevice
        client.open_devices()

        print("VDI device open and ready. Data transfer starting...\n")

        # Phase 4: Command processing loop
        client.process_commands()

        print("Backup session completed.")

    except Exception as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        return 1
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:
                pass

    return 0

if __name__ == "__main__":
    sys.exit(main())