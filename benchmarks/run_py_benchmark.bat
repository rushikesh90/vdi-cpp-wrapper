@echo off
REM Python VDI Benchmark Launcher
REM
REM Usage:
REM   run_py_benchmark.bat [device_name]
REM
REM If device_name is omitted, generates VDI_PyBench_<pid>

setlocal enabledelayedexpansion

set PYTHON_DIR=%~dp0python-3.13.3-embed-amd64
set PYTHON_EXE=%PYTHON_DIR%\python.exe

if not exist "%PYTHON_EXE%" (
    echo ERROR: Python not found at %PYTHON_EXE%
    exit /b 1
)

REM Add the benchmarks directory to sys.path so py_vdi_client can be imported
set PYTHONPATH=%~dp0

if "%~1"=="" (
    "%PYTHON_EXE%" "%~dp0run_py_benchmark.py"
) else (
    "%PYTHON_EXE%" "%~dp0run_py_benchmark.py" "%~1"
)