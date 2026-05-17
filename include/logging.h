#pragma once

// ---------------------------------------------------------------------------
// Logging system for the VDI wrapper project.
//
// Compile-time logging level selection:
//   #define LOGGING_LEVEL 0   // OFF   - no logging code emitted
//   #define LOGGING_LEVEL 1   // INFO  - lifecycle / session events only
//   #define LOGGING_LEVEL 2   // DEBUG - per-chunk tracing (verbose)
//
// If LOGGING_LEVEL is not defined, INFO is the default.
//
// Build configurations (recommended):
//   Release_NoLogging.exe     -DLOGGING_LEVEL=0
//   Release_Info.exe          -DLOGGING_LEVEL=1  (or omit, default)
//   Debug_Trace.exe           -DLOGGING_LEVEL=2
//
// TRACE_EVENT provides RAII scope-based instrumentation:
//   {  TRACE_EVENT("SinkWrite");
//      sink->write(data, size);  }
//   // prints: [TRACE] SinkWrite tid=1234 52 us
//
// LOG_DEBUG is only compiled when LOGGING_LEVEL >= 2.
// LOG_INFO  is only compiled when LOGGING_LEVEL >= 1.
// LOG_ERROR is always compiled.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdint>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Logging level constants ───────────────────────────────────────────────
#ifndef LOGGING_LEVEL
#define LOGGING_LEVEL 1   // default: INFO
#endif

#define LOG_LEVEL_OFF   0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

// ── LOG_ERROR – always compiled ───────────────────────────────────────────
#define LOG_ERROR(...)                                                          \
  do {                                                                          \
    std::fprintf(stderr, "[ERROR] " __VA_ARGS__);                               \
    std::fputc('\n', stderr);                                                   \
  } while (0)

// ── LOG_HRESULT – always compiled ──────────────────────────────────────────
#define LOG_HRESULT(msg, hr)                                                    \
  LOG_ERROR("%s HRESULT=0x%08lx", msg, static_cast<unsigned long>(hr))

// ── LOG_INFO – compiled when LOGGING_LEVEL >= 1 ────────────────────────────
#if LOGGING_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(...)                                                           \
  do {                                                                          \
    std::fprintf(stdout, "[INFO]  " __VA_ARGS__);                               \
    std::fputc('\n', stdout);                                                   \
  } while (0)
#else
#define LOG_INFO(...) ((void)0)
#endif

// ── LOG_WARN – always compiled (like LOG_ERROR) ────────────────────────────
// Use for non-fatal conditions that should be visible even in release builds.
#define LOG_WARN(...)                                                           \
  do {                                                                          \
    std::fprintf(stderr, "[WARN]  " __VA_ARGS__);                               \
    std::fputc('\n', stderr);                                                   \
  } while (0)

// ── LOG_DEBUG – compiled when LOGGING_LEVEL >= 2 ───────────────────────────
#if LOGGING_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...)                                                          \
  do {                                                                          \
    std::fprintf(stderr, "[DEBUG] " __VA_ARGS__);                               \
    std::fputc('\n', stderr);                                                   \
  } while (0)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

// ── TRACE_EVENT – RAII scope timer ─────────────────────────────────────────
// Records start time in constructor, prints elapsed µs + TID in destructor.
// Only compiled when LOGGING_LEVEL >= 2 (DEBUG mode).
#if LOGGING_LEVEL >= LOG_LEVEL_DEBUG

struct TraceEventData {
    const char* name_;
    std::chrono::steady_clock::time_point start_;
    uint64_t start_us_;  // relative to steady_clock epoch (for reference)
#ifdef _WIN32
    DWORD tid_;
#endif

    TraceEventData(const char* name)
        : name_(name),
          start_(std::chrono::steady_clock::now()),
          start_us_(static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  start_.time_since_epoch()).count()))
#ifdef _WIN32
          , tid_(GetCurrentThreadId())
#endif
    {}

    ~TraceEventData() {
        auto end = std::chrono::steady_clock::now();
        auto elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                end - start_).count());
#ifdef _WIN32
        std::fprintf(stderr, "[TRACE] %s tid=%lu %llu us\n",
                     name_, static_cast<unsigned long>(tid_),
                     static_cast<unsigned long long>(elapsed));
#else
        std::fprintf(stderr, "[TRACE] %s %llu us\n",
                     name_, static_cast<unsigned long long>(elapsed));
#endif
    }
};

#define TRACE_EVENT(name) \
    TraceEventData _trace_##__LINE__(name)

#else
// Zero-cost when LOGGING_LEVEL < 2
#define TRACE_EVENT(name) ((void)0)
#endif