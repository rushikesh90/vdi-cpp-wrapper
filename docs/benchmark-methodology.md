# Benchmark Methodology

## Goal

Measure the raw throughput and latency of the VDI command loop in a controlled,
repeatable environment. The benchmark compares C++ (native COM) and Python
(ctypes COM) implementations of the same VDI client logic.

## Workload

| Parameter | Value |
|---|---|
| Backup type | FULL |
| Database size | 5 GB |
| Device count | 1 |
| Sink | NullSink (data discarded) |
| Logging | OFF during benchmark runs |
| Buffer pool | 64 buffers × 64 KB |

**Why NullSink?**  
We measure VDI protocol overhead, not disk I/O. A file or cloud sink would add
storage-latency noise that masks the VDI signal. For end-to-end backup
performance, add your sink of choice and measure separately.

## Environment

All benchmarks run on identically configured instances unless otherwise noted:

| Item | Value |
|---|---|
| VM type | Azure D2s_v5 |
| vCPUs | 2 |
| RAM | 8 GB |
| OS | Windows Server 2022 |
| SQL Server | SQL Server 2022 Developer |
| Storage | Standard SSD (managed disk) |
| C++ compiler | MSVC x64, `/O2`, C++17 |
| Python | 3.13.3 (embedded), ctypes COM |

## Protocol

1. **Cold start**: Terminate any prior VDI sessions, restart SQL Server.
2. **Prepare**: Build C++ binary using the build script.
3. **Launch**: Start the benchmark client — it waits for SQL Server to connect.
4. **Trigger**: In a separate SSMS / sqlcmd window, run:
   ```sql
   BACKUP DATABASE [benchmark_db] TO VIRTUAL_DEVICE = N'VDI_Bench_XXXX'
   ```
5. **Collect**: Client runs until `VD_E_CLOSE`, then emits a JSON summary.
6. **Repeat**: Run 5 times, take the median throughput value.
7. **Reset**: Drop and re-create the database between runs to avoid
   incremental-backup effects.

## Metrics

All metrics are computed from client-side timestamps:

- **Throughput (MB/s)**: `total_bytes / 1024² / elapsed_wall_seconds`
- **Per-stage latencies**: Each chunk's lifecycle is decomposed into four stages:
  - `getcommand_us`: Time blocked waiting for SQL Server to deliver the next command
  - `dispatch_us`: Command dispatch overhead (switch + decision logic)
  - `sink_us`: Time spent in `sink->write()` (the actual I/O)
  - `complete_us`: Time spent in `CompleteCommand()` (SQL sync / flow control)
- **CPU utilization**: User + Kernel CPU time from `GetProcessTimes`, expressed
  as percentage of wall-clock time.

Latencies are reported as P50/P95/P99 in microseconds, computed from the full
`ChunkTiming` vector (not running sums).

## Controlled Variables

- **Same workload**: Database is re-created with the same size before each run.
- **Same sink**: NullSink used in both C++ and Python benchmarks.
- **Same VM**: Benchmarks run on the same Azure instance, back-to-back.
- **Logging disabled**: `LOG_DEBUG` compiled out; `LOGGING_LEVEL=0` during
  benchmark runs.

## Benchmark Caveats

The following limitations apply to all benchmark results:

- **Single-device stream**: All measurements use exactly one virtual device.
  Multi-device parallelism is not tested.
- **No concurrent workloads**: SQL Server runs in isolation with no other
  queries or backups during measurement.
- **Local sink only**: Data is discarded (NullSink) or written to local disk
  (FileSink). No network storage, cloud storage, or tape targets.
- **No compression or encryption**: Raw data only. Compression/encryption
  would add CPU overhead not captured here.
- **NullSink benchmarks measure protocol overhead only**: Results with
  NullSink do not represent realistic backup performance — they isolate the
  VDI protocol path.
- **SQL Server version dependency**: Results may vary with SQL Server version,
  edition, and cumulative update level.
- **Hardware dependency**: Results on different VM SKUs, physical hardware,
  or storage configurations will differ.
- **No restore pipeline**: Only the BACKUP path (VDC_Write) is instrumented.
  RESTORE (VDC_Read) is not tested.

## Known Sources of Variance

- SQL Server internal scheduling (buffer pool flushes, checkpoint)
- Windows scheduler preemption (especially on 2-vCPU VMs)
- VM hypervisor steal time (Azure burstable SKUs)
- Paging (ensure sufficient RAM for both SQL Server and benchmark client)

## Repeatability Check

Run the included repeatability script to measure variance:

```powershell
.\scripts\run_repeatability_check.ps1 -BinaryPath .\build\bin\Release\vdi_wrapper.exe
```

A coefficient of variation (CV) below 2% is considered excellent; below 5%
is acceptable. If CV exceeds 5%, investigate environment noise before
publishing results.

## Reproducibility Checklist

- [ ] Same VM SKU and configuration
- [ ] Same SQL Server version and edition
- [ ] Same database size and name
- [ ] Same VDI device name
- [ ] No other VDI clients running concurrently
- [ ] Logging compiled out (`LOGGING_LEVEL=0`)
- [ ] 5 runs, median reported
- [ ] Sink specified in results (NullSink or FileSink)
- [ ] Environment metadata captured (OS, CPU, RAM, SQL version)