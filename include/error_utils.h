#pragma once

// ---------------------------------------------------------------------------
// Error handling utilities for the VDI wrapper.
//
// Provides:
//   - SessionState enum for explicit state machine tracking
//   - hresult_to_string() to translate HRESULT codes to human-readable text
//   - session_state_to_string() for logging and debugging
// ---------------------------------------------------------------------------

#include <string>
#include <cstdint>

// ── Session State Machine ─────────────────────────────────────────────────
// Explicit session states prevent invalid transitions, double-close bugs,
// and provide clear error context.
enum class SessionState {
    INIT,          // Object constructed, nothing initialized
    CONNECTED,     // CoCreateInstance + CreateEx succeeded
    STREAMING,     // OpenDevice succeeded, command loop running
    FLUSHING,      // VD_E_CLOSE received, sink flush in progress
    CLOSED,        // All devices closed, COM uninitialized
    FAILED         // Unrecoverable error occurred
};

// Convert SessionState to string for logging
inline const char* session_state_to_string(SessionState state) {
    switch (state) {
    case SessionState::INIT:       return "INIT";
    case SessionState::CONNECTED:  return "CONNECTED";
    case SessionState::STREAMING:  return "STREAMING";
    case SessionState::FLUSHING:   return "FLUSHING";
    case SessionState::CLOSED:     return "CLOSED";
    case SessionState::FAILED:     return "FAILED";
    default:                       return "UNKNOWN";
    }
}

// Translate an HRESULT to a human-readable string.
// Covers all VDI protocol error codes and common COM/system codes.
std::string hresult_to_string(unsigned long hr);