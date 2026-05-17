# VDI Wrapper Scripts

This directory contains helper scripts for the VDI C++ Wrapper.

## Files

### `run_vdi_wrapper.bat`
A batch file that shows usage examples for the VDI wrapper executables.

Usage:
```cmd
run_vdi_wrapper.bat
```

### `simple_auto_test.ps1`
A PowerShell script that demonstrates the automated VDI workflow.

Usage:
```powershell
.\simple_auto_test.ps1
```

### `backup.sql`
SQL script containing the standardized backup command for VDI testing.

Usage:
```sql
BACKUP DATABASE TestDB
TO VIRTUAL_DEVICE = 'MyVDIDevice'
WITH FORMAT, INIT, STATS = 5;
```

### `create_testdb.sql`
SQL script to create a test database for benchmarking.

Usage:
```sql
CREATE DATABASE BenchmarkDB;
```

### `run_cpp_nullsink.ps1`
PowerShell script to run C++ VDI backup with Null sink.

Usage:
```powershell
.\run_cpp_nullsink.ps1 -BenchmarkId "run1"
```

### `run_cpp_filesink.ps1`
PowerShell script to run C++ VDI backup with File sink.

Usage:
```powershell
.\run_cpp_filesink.ps1 -BenchmarkId "run1"
```

### `run_python_nullsink.ps1`
PowerShell script to run Python VDI backup with Null sink.

Usage:
```powershell
.\run_python_nullsink.ps1 -BenchmarkId "run1"
```

## About the VDI Wrapper

The VDI (Virtual Device Interface) wrapper is designed to work with SQL Server's backup system. It follows this workflow:

1. **Start the VDI wrapper** - It generates a unique device name
2. **Execute SQL command** - Run a BACKUP DATABASE command pointing to the VDI device
3. **Process data** - The wrapper receives and processes the backup data stream
4. **Collect metrics** - Performance statistics are gathered and displayed

## Modes of Operation

### Interactive Mode
```cmd
vdi_wrapper_null.exe
```
- Shows device name and usage instructions
- Waits for user to manually execute the SQL command
- Best for learning and testing

### Automated Mode
```cmd
vdi_wrapper_null.exe --auto
```
- Automatically launches `sqlcmd` to execute the backup command
- Requires `sqlcmd` to be available on PATH
- Best for automated testing and benchmarking

## Sink Types

### Null Sink (default)
```cmd
vdi_wrapper_null.exe
```
- Discards all backup data
- Useful for performance testing without storage overhead

### File Sink
```cmd
vdi_wrapper_file.exe
```
- Writes backup data to `backup_stream.bin`
- Useful for capturing actual backup data for analysis

## Benchmarking Features

### Benchmark ID Support
All scripts support the `--benchmark-id` parameter to correlate logs and metrics:
```cmd
vdi_wrapper_null.exe --benchmark-id "cpp_nullsink_run1"
```

### Automatic Result Capture
Scripts automatically export metrics to JSON files in `benchmarks/results/`:
- `cpp_nullsink_<benchmark_id>_<date>.json`
- `cpp_filesink_<benchmark_id>_<date>.json`
- `python_nullsink_<benchmark_id>_<date>.json`

## Requirements

- Windows Server 2022 or Windows 11
- SQL Server 2019+ (for actual VDI usage)
- Visual Studio 2022 with C++ development tools
- Python 3.13 (for Python benchmarks)
