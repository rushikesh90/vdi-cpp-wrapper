<#
.SYNOPSIS
    Python VDI Benchmark — run, collect JSON output.

.DESCRIPTION
    1. Locates the embedded Python interpreter in benchmarks/ directory.
    2. Runs the Python VDI benchmark (NullSink mode).
    3. Captures output into a timestamped file.
    4. Saves extracted JSON metrics to benchmarks/results/.

    Prerequisites:
      - SQL Server 2019+ running on the same machine (or accessible)
      - Python 3.13+ (embedded dist in benchmarks/python-3.13.3-embed-amd64/)
        OR Python installed system-wide

.EXAMPLE
    .\scripts\run_python_benchmark.ps1
#>

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$ResultsDir = "$RepoRoot\benchmarks\results"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutFile = "$ResultsDir\py_benchmark_$Timestamp.json"

# Ensure results directory exists
if (-not (Test-Path $ResultsDir)) {
    New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
}

Write-Host "=== Python VDI Benchmark ===" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Locate Python ----
Write-Host "[1/3] Locating Python interpreter..." -ForegroundColor Yellow

# Try embedded Python first, then fall back to system Python
$PythonExe = $null
$EmbeddedPython = "$RepoRoot\benchmarks\python-3.13.3-embed-amd64\python.exe"
if (Test-Path $EmbeddedPython) {
    $PythonExe = $EmbeddedPython
    Write-Host "  Using embedded Python: $PythonExe" -ForegroundColor Green
} else {
    # Try system Python
    $PythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $PythonExe) {
        Write-Error "Python not found. Install Python 3.13+ or place embedded dist at: $EmbeddedPython"
        exit 1
    }
    Write-Host "  Using system Python: $PythonExe" -ForegroundColor Green
}

$PythonScript = "$RepoRoot\benchmarks\python\run_py_benchmark.py"
if (-not (Test-Path $PythonScript)) {
    Write-Error "Python benchmark script not found: $PythonScript"
    exit 1
}

Write-Host ""

# ---- Step 2: Setup PYTHONPATH ----
# Ensure the benchmarks/python directory and embedded lib dir are on sys.path
$env:PYTHONPATH = "$RepoRoot\benchmarks\python"

# ---- Step 3: Run benchmark ----
Write-Host "[2/3] Ready for backup." -ForegroundColor Yellow
Write-Host ""
Write-Host "The Python script will print a device name. Use that in SSMS:" -ForegroundColor Magenta
Write-Host ""
Write-Host "    BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'<device_name>'" -ForegroundColor White
Write-Host ""

Write-Host "[3/3] Running Python benchmark..." -ForegroundColor Yellow
Write-Host "Output will be saved to: $OutFile" -ForegroundColor Cyan
Write-Host ""

# Run and capture all output
& $PythonExe $PythonScript *> $OutFile
$exitCode = $LASTEXITCODE

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "Benchmark completed successfully." -ForegroundColor Green
} else {
    Write-Host "Benchmark exited with code $exitCode." -ForegroundColor Red
}

Write-Host "Full output logged to: $OutFile" -ForegroundColor Cyan

# ---- Extract JSON metrics ----
$output = Get-Content $OutFile -Raw
if ($output -match "---BEGIN JSON METRICS---\s*\n(.*?)\n---END JSON METRICS---") {
    $json = $Matches[1]
    $json | Out-File -FilePath "$ResultsDir\py_benchmark_${Timestamp}_metrics.json" -Encoding utf8
    Write-Host "Metrics JSON saved to: py_benchmark_${Timestamp}_metrics.json" -ForegroundColor Green
    Write-Host ""
    Write-Host "Extracted metrics:" -ForegroundColor Cyan
    $json
} else {
    Write-Host "Warning: Could not extract JSON metrics from output." -ForegroundColor Yellow
    Write-Host "(Python benchmark may not emit JSON markers yet — check py_vdi_client.py)"
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan