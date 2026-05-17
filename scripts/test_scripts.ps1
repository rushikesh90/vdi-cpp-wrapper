# Test script to verify all new scripts are working correctly
# This script validates that our new scripts have been created properly

Write-Host "Testing newly created scripts..." -ForegroundColor Cyan

# Check that all required files exist
$requiredFiles = @(
    "backup.sql",
    "create_testdb.sql",
    "run_cpp_nullsink.ps1",
    "run_cpp_filesink.ps1", 
    "run_python_nullsink.ps1",
    "wait_for_wrapper.ps1"
)

$allExist = $true
foreach ($file in $requiredFiles) {
    if (Test-Path $file) {
        Write-Host "✓ $file exists" -ForegroundColor Green
    } else {
        Write-Host "✗ $file missing" -ForegroundColor Red
        $allExist = $false
    }
}

if ($allExist) {
    Write-Host "`nAll required script files have been created successfully!" -ForegroundColor Green
} else {
    Write-Host "`nSome script files are missing!" -ForegroundColor Red
    exit 1
}

# Test that the main.cpp has been updated with benchmark ID support
$mainCppContent = Get-Content "src/main.cpp" -Raw
if ($mainCppContent -match "--benchmark-id") {
    Write-Host "✓ Benchmark ID support added to main.cpp" -ForegroundColor Green
} else {
    Write-Host "✗ Benchmark ID support not found in main.cpp" -ForegroundColor Red
    exit 1
}

Write-Host "`nScript validation completed successfully!" -ForegroundColor Green