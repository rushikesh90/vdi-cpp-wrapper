# PowerShell script to run Python VDI backup with Null sink
# This script automates the Python VDI backup process with proper synchronization

param(
    [string]$BenchmarkId = "default_run"
)

# Define paths
$PythonScriptPath = ".\benchmarks\python\run_py_benchmark.py"
$SqlScriptPath = ".\backup.sql"
$ResultsPath = ".\benchmarks\results"

# Create results directory if it doesn't exist
if (!(Test-Path $ResultsPath)) {
    New-Item -ItemType Directory -Path $ResultsPath
}

# Generate unique device name with benchmark ID
$DeviceName = "MyVDIDevice_$BenchmarkId"

Write-Host "Starting Python Null Sink VDI Backup with Benchmark ID: $BenchmarkId" -ForegroundColor Green
Write-Host "Using device name: $DeviceName" -ForegroundColor Yellow

# Execute the Python benchmark script
Write-Host "Running Python benchmark script..." -ForegroundColor Cyan
python .\benchmarks\python\run_py_benchmark.py --benchmark-id $BenchmarkId

# Export metrics to JSON file
$timestamp = Get-Date -Format "yyyy-MM-dd"
$metricsFileName = "python_nullsink_$BenchmarkId`_$timestamp.json"
$metricsPath = Join-Path $ResultsPath $metricsFileName

# Create a basic result file
$metricsContent = @{
    benchmark_id = $BenchmarkId
    timestamp = Get-Date -Format "o"
    device_name = $DeviceName
    script_version = "1.0"
    python_script = $PythonScriptPath
} | ConvertTo-Json

$metricsContent | Out-File -FilePath $metricsPath -Encoding UTF8

Write-Host "Metrics exported to: $metricsPath" -ForegroundColor Green