#include "environment.h"
#include "logging.h"
#include "version.h"

#if defined(_WIN32)
#include <windows.h>
#endif

EnvironmentInfo capture_environment() {
    EnvironmentInfo info = {};
    info.sql_version = "";  // best-effort, may remain empty

#if defined(_WIN32)
    // ── OS name and version ──────────────────────────────────────────────
    // Use GetVersionExW as a best-effort approach. For Windows 10+,
    // the version may be 10.0; we attempt to read the registry for
    // the product name.
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        WCHAR buf[256] = {};
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(hkey, L"ProductName", NULL, NULL,
                (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                info.os_name.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &info.os_name[0], len, NULL, NULL);
            }
        }

        size = sizeof(buf);
        if (RegQueryValueExW(hkey, L"CurrentVersion", NULL, NULL,
                (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                info.os_version.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &info.os_version[0], len, NULL, NULL);
            }
        }
        RegCloseKey(hkey);
    }

    // ── CPU model ────────────────────────────────────────────────────────
    // Use the CPUID approach via __cpuid. For a simpler portable approach,
    // read the processor name from the registry.
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        WCHAR buf[256] = {};
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(hkey, L"ProcessorNameString", NULL, NULL,
                (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                info.cpu_model.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &info.cpu_model[0], len, NULL, NULL);
            }
        }
        RegCloseKey(hkey);
    }

    // ── CPU core count ───────────────────────────────────────────────────
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    info.cpu_core_count = static_cast<unsigned>(sys_info.dwNumberOfProcessors);

    // ── RAM ──────────────────────────────────────────────────────────────
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        info.ram_bytes = mem_status.ullTotalPhys;
    }

    // ── SQL Server version (best-effort) ─────────────────────────────────
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Microsoft SQL Server\\MSSQL16.MSSQLSERVER\\Setup",
            0, KEY_READ | KEY_WOW64_64KEY, &hkey) == ERROR_SUCCESS) {
        WCHAR buf[128] = {};
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(hkey, L"PatchLevel", NULL, NULL,
                (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                info.sql_version.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &info.sql_version[0], len, NULL, NULL);
            }
        }
        RegCloseKey(hkey);
    }
#else
    (void)info;  // unused on non-Windows
#endif

    return info;
}

std::string environment_to_json(const EnvironmentInfo& env) {
    std::string json;
    json += "  \"environment\": {\n";
    json += "    \"os_name\": \"" + env.os_name + "\",\n";
    json += "    \"os_version\": \"" + env.os_version + "\",\n";
    json += "    \"cpu_model\": \"" + env.cpu_model + "\",\n";
    json += "    \"cpu_core_count\": " + std::to_string(env.cpu_core_count) + ",\n";
    json += "    \"ram_bytes\": " + std::to_string(env.ram_bytes) + ",\n";
    json += "    \"sql_version\": \"" + env.sql_version + "\",\n";
    json += "    \"tool_version\": \"" VDI_WRAPPER_VERSION_STRING "\"\n";
    json += "  }";
    return json;
}