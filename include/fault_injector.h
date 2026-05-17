#pragma once

// ---------------------------------------------------------------------------
// Fault injection harness for robustness testing.
//
// Gated by the compile-time constant FAULT_INJECTION_ENABLED:
//   - 0: zero-cost no-op (all methods inline, no state allocated)
//   - 1: counters and hooks active
//
// Usage:
//   FaultInjector injector;
//   // After N successful chunks, the next CompleteCommand will fail
//   injector.set_fail_after_n_chunks(100);
//   ...
//   if (injector.should_fail()) { ... return failure; }
//
// This is a development/testing tool, not intended for production builds.
// ---------------------------------------------------------------------------

#include <cstdint>

#ifndef FAULT_INJECTION_ENABLED
#define FAULT_INJECTION_ENABLED 0
#endif

#if FAULT_INJECTION_ENABLED

class FaultInjector {
public:
    FaultInjector()
        : fail_after_n_chunks_(0),
          chunk_counter_(0),
          fail_next_command_(false) {}

    // Schedule a failure after 'n' successful chunk completions.
    // Set to 0 to disable (default).
    void set_fail_after_n_chunks(uint64_t n) {
        fail_after_n_chunks_ = n;
        chunk_counter_ = 0;
    }

    // Fail the very next command (single-shot).
    void set_fail_next_command() {
        fail_next_command_ = true;
    }

    // Called after each successful CompleteCommand to advance the counter.
    // Returns true if the next call to should_fail() should return true.
    bool on_chunk_completed() {
        if (fail_after_n_chunks_ > 0) {
            ++chunk_counter_;
            return chunk_counter_ >= fail_after_n_chunks_;
        }
        return false;
    }

    // Query whether the current operation should fail.
    // Consumes the fail-next-command flag (single-shot).
    bool should_fail() {
        if (fail_next_command_) {
            fail_next_command_ = false;
            return true;
        }
        if (fail_after_n_chunks_ > 0 && chunk_counter_ >= fail_after_n_chunks_) {
            return true;
        }
        return false;
    }

    // Reset all failure modes.
    void reset() {
        fail_after_n_chunks_ = 0;
        chunk_counter_ = 0;
        fail_next_command_ = false;
    }

    // Checkpoint helpers for testing
    uint64_t chunk_count() const { return chunk_counter_; }
    bool is_armed() const {
        return fail_after_n_chunks_ > 0 || fail_next_command_;
    }

private:
    uint64_t fail_after_n_chunks_;
    uint64_t chunk_counter_;
    bool     fail_next_command_;
};

#else // FAULT_INJECTION_ENABLED == 0

// Zero-cost stub — no state, no code emitted in the hot path.
class FaultInjector {
public:
    void set_fail_after_n_chunks(uint64_t) {}
    void set_fail_next_command() {}
    bool on_chunk_completed() { return false; }
    bool should_fail() { return false; }
    void reset() {}
    uint64_t chunk_count() const { return 0; }
    bool is_armed() const { return false; }
};

#endif // FAULT_INJECTION_ENABLED