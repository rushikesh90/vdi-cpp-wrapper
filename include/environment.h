#pragma once

// ---------------------------------------------------------------------------
// Environment information capture for benchmark reproducibility.
//
// Captures OS version, CPU model and core count, total RAM, and SQL Server
// version (when available). All fields are serialisable to JSON.
//
// This enables:
//   - Reproducible benchmark reporting
//   - Cross-environment comparison
//   - Automated validation of environment prerequisites
// ---------------------------------------------------------------------------

#include <string>
#include <cstdint>

struct EnvironmentInfo {
    std::string os_name;              // "Windows Server 2022"
    std::string os_version;           // "10.0.20348"
    std::string cpu_model;            // "AMD EPYC 7763 64-Core Processor"
    unsigned    cpu_core_count;       // Logical processor count
    uint64_t    ram_bytes;            // Total physical RAM in bytes
    std::string sql_version;          // "16.0.1000.6" or empty if not detected
};

// Capture the current environment.
// Never throws — returns best-effort values with empty strings on failure.
EnvironmentInfo capture_environment();

// Serialise environment to JSON string.
std::string environment_to_json(const EnvironmentInfo& env);