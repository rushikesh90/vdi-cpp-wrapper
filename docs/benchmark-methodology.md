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

All benchmarks run identically configured instances:

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

All metrics are computed from the client-side timestamps:

- **Throughput (MB/s)**: `total_bytes / 1024² / elapsed_wall_seconds`
- **GetCommand latency**: Time spent blocked waiting for SQL Server to deliver
  the next command (P50 / P95 / P99 in µs).
- **Chunk processing latency**: Time from receiving a `VDC_Write` to calling
  `CompleteCommand` (P50 / P95 / P99 in µs).
- **CPU utilization**: User + Kernel CPU time from `GetProcessTimes`, expressed
  as percentage of wall-clock time.

## Controlled Variables

- **Same workload**: Database is re-created with the same size before each run.
- **Same sink**: NullSink used in both C++ and Python benchmarks.
- **Same VM**: Benchmarks run on the same Azure instance, back-to-back.
- **Logging disabled**: `LOG_DEBUG` is compiled out; `LOG_INFO` suppressed at
  runtime during benchmark runs.

## Known Sources of Variance

- SQL Server internal scheduling (buffer pool flushes, checkpoint)
- Windows scheduler preemption (especially on 2-vCPU VMs)
- VM hypervisor steal time (Azure burstable SKUs)
- Paging (ensure sufficient RAM for both SQL Server and benchmark client)

## Reproducibility Checklist

- [ ] Same VM SKU and configuration
- [ ] Same SQL Server version and edition
- [ ] Same database size and name
- [ ] Same VDI device name
- [ ] No other VDI clients running concurrently
- [ ] Logging compiled out (`/DLOG_ENABLE_DEBUG` NOT defined)
- [ ] 5 runs, median reported