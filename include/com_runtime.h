#pragma once

// ---------------------------------------------------------------------------
// RAII wrapper for COM runtime initialisation.
//
// Usage:
//   {
//       ComRuntime com;
//       if (!com.initialized()) { return; }
//       // ... COM operations ...
//   }  // ~ComRuntime calls CoUninitialize()
//
// This eliminates scattered CoInitializeEx/CoUninitialize calls and
// guarantees correct cleanup even in exceptional paths.
// ---------------------------------------------------------------------------

#include <cstdint>

class ComRuntime {
public:
    ComRuntime();
    ~ComRuntime();

    // No copy or move
    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;
    ComRuntime(ComRuntime&&) = delete;
    ComRuntime& operator=(ComRuntime&&) = delete;

    // Returns true if COM initialised successfully.
    bool initialized() const { return initialized_; }

    // Returns the HRESULT from CoInitializeEx (useful for diagnostics).
    unsigned long init_result() const { return init_result_; }

    // Returns true if COM was already initialized on this thread (S_FALSE).
    bool was_already_initialized() const { return already_init_; }

private:
    bool initialized_;
    unsigned long init_result_;
    bool already_init_;
};