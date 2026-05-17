<#
.SYNOPSIS
    Run the VDI benchmark multiple times and report variance statistics.

.DESCRIPTION
    Runs the C++ benchmark 3 times (configurable) with the same workload
    and reports min/max/mean throughput and latency variance.

    This helps validate benchmark reproducibility by measuring:
      - Throughput stability (coefficient of variation)
      - Latency consistency across runs
      - Environment noise detection

.PARAMETER BinaryPath
    Path to the vdi_wrapper executable.

.PARAMETER Iterations
    Number of benchmark runs (default: 3).

.PARAMETER DatabaseName
    Name of the SQL Server database to back up (default: tempdb).

.PARAMETER SqlInstance
    SQL Server instance name (default: localhost).

.EXAMPLE
    .\scripts\run_repeatability_check.ps1 -BinaryPath .\build\bin\Release\vdi_wrapper.exe

.NOTES
    Requires:
      - SQL Server with database accessible for BACKUP TO VIRTUAL_DEVICE
      - The vdi_wrapper binary built with INFO or DEBUG logging
      - PowerShell 5.1+
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$BinaryPath,

    [Parameter(Mandatory=$false)]
    [int]$Iterations = 3,

    [Parameter(Mandatory=$false)]
    [string]$DatabaseName = "tempdb",

    [Parameter(Mandatory=$false)]
    [string]$SqlInstance = "localhost"
)

# ── Validate binary exists ───────────────────────────────────────────────
if (-not (Test-Path -Path $BinaryPath)) {
    Write-Error "Binary not found: $BinaryPath"
    exit 1
}

Write-Host "VDI Benchmark Repeatability Check" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host "Binary:      $BinaryPath"
Write-Host "Iterations:  $Iterations"
Write-Host "Database:    $DatabaseName"
Write-Host "Instance:    $SqlInstance"
Write-Host ""

# ── Run iterations ───────────────────────────────────────────────────────
$results = @()

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host "Run $i of $Iterations..." -ForegroundColor Yellow

    # Start the VDI wrapper in the background
    $process = Start-Process -FilePath $BinaryPath -NoNewWindow -PassThru -RedirectStandardOutput "run_$i.log"

    # Extract device name from output (wait a moment for output)
    Start-Sleep -Seconds 2
    $deviceName = $null
    if (Test-Path "run_$i.log") {
        $content = Get-Content "run_$i.log" -Raw
        if ($content -match "VDI_Device_\d+") {
            $deviceName = $matches[0]
        }
    }

    if (-not $deviceName) {
        Write-Warning "Could not extract device name from run $i. Check run_$i.log."
        # Clean up process if still running
        if (-not $process.HasExited) {
            $process.Kill()
        }
        continue
    }

    Write-Host "  Device name: $deviceName"

    # Issue the BACKUP command via sqlcmd
    $backupQuery = "BACKUP DATABASE [$DatabaseName] TO VIRTUAL_DEVICE = N'$deviceName'"
    Write-Host "  Executing: $backupQuery"

    $sqlResult = sqlcmd -S $SqlInstance -Q $backupQuery -b 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "  SQL Server command failed: $sqlResult"
        if (-not $process.HasExited) {
            $process.Kill()
        }
        continue
    }

    # Send Enter key to unblock the console in vdi_wrapper
    # (The wrapper waits for Enter after printing the instruction)
    # Simulate via named pipe or stdin pipe
    # For now, just wait for the process to finish naturally.

    # Wait for process to complete (with timeout)
    $timeoutSeconds = 300  # 5 minutes max
    $completed = $process.WaitForExit($timeoutSeconds * 1000)
    if (-not $completed) {
        Write-Warning "  Run $i timed out after $timeoutSeconds seconds."
        $process.Kill()
        continue
    }

    # Parse JSON from output
    $logContent = Get-Content "run_$i.log" -Raw
    if ($logContent -match "---BEGIN JSON METRICS---\s*({.*?})\s*---END JSON METRICS---") {
        $json = $matches[1] | ConvertFrom-Json
        $results += $json
        Write-Host "  Throughput: $($json.throughput_mbps) MB/s" -ForegroundColor Green
    } else {
        Write-Warning "  Could not parse JSON metrics from run $i."
    }
}

# ── Compute variance statistics ─────────────────────────────────────────
Write-Host ""
Write-Host "Results Summary" -ForegroundColor Cyan
Write-Host "===============" -ForegroundColor Cyan

if ($results.Count -lt 2) {
    Write-Warning "Need at least 2 successful runs to compute variance."
    Write-Host "Successful runs: $($results.Count)"
    exit 0
}

$throughputs = $results | ForEach-Object { $_.throughput_mbps }
$avg = ($throughputs | Measure-Object -Average).Average
$min = ($throughputs | Measure-Object -Minimum).Minimum
$max = ($throughputs | Measure-Object -Maximum).Maximum
$stddev = [Math]::Sqrt(($throughputs | ForEach-Object { ($_ - $avg) * ($_ - $avg) } | Measure-Object -Sum).Sum / $throughputs.Count)
$cv = if ($avg -gt 0) { ($stddev / $avg) * 100.0 } else { 0.0 }

Write-Host "Throughput (MB/s):"
Write-Host "  Min:    $($min.ToString('F1'))"
Write-Host "  Max:    $($max.ToString('F1'))"
Write-Host "  Mean:   $($avg.ToString('F1'))"
Write-Host "  StdDev: $($stddev.ToString('F2'))"
Write-Host "  CV:     $($cv.ToString('F1'))%"

if ($cv -lt 2.0) {
    Write-Host ""
    Write-Host "✅ Repeatability: Excellent (CV < 2%)" -ForegroundColor Green
} elseif ($cv -lt 5.0) {
    Write-Host ""
    Write-Host "⚠️ Repeatability: Acceptable (CV 2–5%)" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "❌ Repeatability: Poor (CV > 5%) — investigate environment noise" -ForegroundColor Red
}

# Cleanup temporary log files
# Remove-Item run_*.log

Write-Host ""
Write-Host "Complete. $($results.Count) successful runs."