<#
.SYNOPSIS
    C++ VDI Benchmark — build, run, collect JSON output.

.DESCRIPTION
    1. Builds vdi_wrapper.exe in Release mode.
    2. Prompts you to execute the T-SQL BACKUP command in SSMS.
    3. Runs the benchmark and captures stdout into a JSON file.
    4. Saves the JSON to benchmarks/results/cpp_benchmark_<timestamp>.json.

    Prerequisites:
      - Visual Studio 2022 (or MSVC toolchain in PATH)
      - CMake 3.16+
      - SQL Server 2019+ running on the same machine (or accessible)

.EXAMPLE
    .\scripts\run_cpp_benchmark.ps1
#>

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$BuildDir = "$RepoRoot\build"
$ResultsDir = "$RepoRoot\benchmarks\results"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutFile = "$ResultsDir\cpp_benchmark_$Timestamp.json"

# Ensure results directory exists
if (-not (Test-Path $ResultsDir)) {
    New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
}

Write-Host "=== C++ VDI Benchmark ===" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Build ----
Write-Host "[1/3] Building vdi_wrapper (Release)..." -ForegroundColor Yellow

# Configure CMake (if not already configured)
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Host "  Configuring CMake..."
    cmake -S $RepoRoot -B $BuildDir -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed."
        exit 1
    }
}

# Build
Write-Host "  Building..."
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit 1
}

# Locate the binary
$Binary = Get-ChildItem -Path $BuildDir -Recurse -Filter "vdi_wrapper.exe" | Select-Object -First 1
if (-not $Binary) {
    Write-Error "vdi_wrapper.exe not found after build."
    exit 1
}

Write-Host "  Built: $($Binary.FullName)" -ForegroundColor Green
Write-Host ""

# ---- Step 2: Prompt for T-SQL ----
Write-Host "[2/3] Ready for backup." -ForegroundColor Yellow
Write-Host ""
Write-Host "In SQL Server Management Studio (or sqlcmd), execute:" -ForegroundColor Magenta
Write-Host ""
Write-Host "    BACKUP DATABASE [tempdb] TO VIRTUAL_DEVICE = N'VDI_Bench_<pid>'" -ForegroundColor White
Write-Host ""

# We need to know the device name before running: the binary generates VDI_Device_<pid>
# Run the binary with a flag or just parse its output.
# Actually, the binary prints the device name on startup. We'll just run it.

# ---- Step 3: Run benchmark and capture ----
Write-Host "[3/3] Running benchmark (press Enter in the app when T-SQL has been issued)..." -ForegroundColor Yellow
Write-Host ""

# Run the binary, tee output to both console and file
Write-Host "Output will be saved to: $OutFile" -ForegroundColor Cyan
Write-Host ""

# Create a unique result file per run
& $Binary.FullName *> $OutFile
$exitCode = $LASTEXITCODE

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "Benchmark completed successfully." -ForegroundColor Green
} else {
    Write-Host "Benchmark exited with code $exitCode." -ForegroundColor Red
}

Write-Host "Full output logged to: $OutFile" -ForegroundColor Cyan

# ---- Summary ----
# Extract JSON from the output (between BEGIN/END JSON METRICS markers)
$output = Get-Content $OutFile -Raw
if ($output -match "---BEGIN JSON METRICS---\s*\n(.*?)\n---END JSON METRICS---") {
    $json = $Matches[1]
    $json | Out-File -FilePath "$ResultsDir\cpp_benchmark_${Timestamp}_metrics.json" -Encoding utf8
    Write-Host "Metrics JSON saved to: cpp_benchmark_${Timestamp}_metrics.json" -ForegroundColor Green
    Write-Host ""
    Write-Host "Extracted metrics:" -ForegroundColor Cyan
    $json
} else {
    Write-Host "Warning: Could not extract JSON metrics from output." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan