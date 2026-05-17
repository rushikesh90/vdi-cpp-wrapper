#pragma once

// ---------------------------------------------------------------------------
// Timer abstraction for high-resolution per-chunk instrumentation.
//
// Provides two backends:
//   ChronoTimer – portable default using std::chrono::steady_clock
//   QPCTimer    – Windows-only using QueryPerformanceCounter
//
// The abstract Timer interface returns microseconds as uint64_t.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Abstract timer interface ──────────────────────────────────────────────
class Timer {
public:
    virtual ~Timer() = default;

    // Returns the current time in microseconds since an unspecified epoch
    // (must be monotonically non-decreasing within a session).
    virtual uint64_t now_us() = 0;
};

// ── ChronoTimer – portable default ─────────────────────────────────────────
class ChronoTimer : public Timer {
public:
    ChronoTimer()
        : epoch_(std::chrono::steady_clock::now()) {}

    uint64_t now_us() override {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - epoch_).count());
    }

private:
    std::chrono::steady_clock::time_point epoch_;
};

// ── QPCTimer – Windows QueryPerformanceCounter ─────────────────────────────
#ifdef _WIN32
class QPCTimer : public Timer {
public:
    QPCTimer() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        freq_ = freq.QuadPart;
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        start_ = start.QuadPart;
    }

    uint64_t now_us() override {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        uint64_t elapsed_ticks = now.QuadPart - start_;
        // Convert to microseconds: (elapsed_ticks * 1'000'000) / freq
        return (elapsed_ticks * 1'000'000ULL) / freq_;
    }

private:
    uint64_t freq_;
    uint64_t start_;
};
#endif

// ── Timer factory ──────────────────────────────────────────────────────────
enum class TimerMode {
    Chrono,
    QPC
};

inline Timer* create_timer(TimerMode mode) {
    switch (mode) {
    case TimerMode::Chrono:
        return new ChronoTimer();
#ifdef _WIN32
    case TimerMode::QPC:
        return new QPCTimer();
#endif
    default:
        return new ChronoTimer();
    }
}