# Quickstart

> **Get your first VDI benchmark in under 30 minutes.**

---

## Prerequisites

- Windows Server 2022+ or Windows 11
- SQL Server 2019+ (Developer Edition is free)
- Visual Studio 2022 with "Desktop development with C++"
- CMake 3.16+ (or use the provided MSVC CLI commands)

---

## Step 1: Clone

```powershell
git clone https://github.com/rushikesh90/vdi-cpp-wrapper.git
cd vdi-cpp-wrapper
```

## Step 2: Build

### Using CMake

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The binary will be at `build\bin\Release\vdi_wrapper.exe`.

### Using MSVC CLI (no CMake)

```powershell
# Open "x64 Native Tools Command Prompt for VS 2022" first
cl src\*.cpp /EHsc /std:c++17 /I include /Fe:vdi_wrapper.exe ole32.lib
```

## Step 3: Run

```powershell
.\vdi_wrapper.exe
```

The program prints a device name like `VDI_Device_1234`.

## Step 4: Start Backup

In SQL Server Management Studio (SSMS) or `sqlcmd`:

```sql
BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'VDI_Device_1234'
```

## Step 5: Observe

Press Enter in the console after issuing the T-SQL command. Watch throughput
and latency metrics stream in real time.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `CoCreateInstance failed` | VDI SDK not registered | Install SQL Server or re-run `regsvr32 sqlvdi.dll` |
| `VD_E_NOTSUPPORTED` | Wrong `CreateEx` signature | Ensure 3-argument call (see `vdi_client.cpp`) |
| `VD_E_INVALID` | Incomplete `VDConfig` | Ensure all 11 fields are set (see `vdi_client.cpp`) |
| Compile error: `sqlvdi.h` not found | VDI SDK not installed | Install SQL Server Developer Edition |
| ESP not saved / stack corruption | Wrong vtable layout | Ensure `IClientVirtualDeviceSet2 : public IClientVirtualDeviceSet` |
| No output from backup | Wrong device name | Copy the exact name printed by `vdi_wrapper.exe` |

---

## Next Steps

- Read [docs/benchmark-methodology.md](docs/benchmark-methodology.md) for reproducible benchmarks
- Read [docs/design-decisions.md](docs/design-decisions.md) for architectural rationale
- Read [docs/glossary.md](docs/glossary.md) for terminology
- Read [docs/operational-behavior.md](docs/operational-behavior.md) for failure handling