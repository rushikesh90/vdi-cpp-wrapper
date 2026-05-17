# PowerShell script to run C++ VDI backup with Null sink
# This script automates the VDI backup process with proper synchronization

param(
    [string]$BenchmarkId = "default_run"
)

# Define paths
$WrapperPath = ".\vdi_wrapper_null.exe"
$SqlScriptPath = ".\backup.sql"
$ResultsPath = ".\benchmarks\results"

# Create results directory if it doesn't exist
if (!(Test-Path $ResultsPath)) {
    New-Item -ItemType Directory -Path $ResultsPath
}

# Generate unique device name with benchmark ID
$DeviceName = "MyVDIDevice_$BenchmarkId"

Write-Host "Starting C++ Null Sink VDI Backup with Benchmark ID: $BenchmarkId" -ForegroundColor Green
Write-Host "Using device name: $DeviceName" -ForegroundColor Yellow

# Start the VDI wrapper in automated mode
$wrapperProcess = Start-Process -FilePath $WrapperPath -ArgumentList "--auto" -PassThru

# Wait a moment for the wrapper to initialize
Start-Sleep -Seconds 2

# Execute the SQL backup command
Write-Host "Executing SQL backup command..." -ForegroundColor Cyan
sqlcmd -S localhost -E -i $SqlScriptPath

# Wait for the wrapper to complete
$wrapperProcess.WaitForExit()

# Capture exit code
$exitCode = $wrapperProcess.ExitCode
Write-Host "Wrapper process exited with code: $exitCode" -ForegroundColor Magenta

# Export metrics to JSON file
$timestamp = Get-Date -Format "yyyy-MM-dd"
$metricsFileName = "cpp_nullsink_$BenchmarkId`_$timestamp.json"
$metricsPath = Join-Path $ResultsPath $metricsFileName

# Try to find and copy metrics output (this assumes the wrapper outputs metrics to console or file)
# For now, we'll just create a basic result file
$metricsContent = @{
    benchmark_id = $BenchmarkId
    timestamp = Get-Date -Format "o"
    wrapper_exit_code = $exitCode
    device_name = $DeviceName
    script_version = "1.0"
} | ConvertTo-Json

$metricsContent | Out-File -FilePath $metricsPath -Encoding UTF8

Write-Host "Metrics exported to: $metricsPath" -ForegroundColor Green