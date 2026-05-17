#pragma once

// ---------------------------------------------------------------------------
// Internal VDI protocol handling.
//
// This header is NOT part of the public API. It isolates all direct
// interaction with VDI COM types (IClientVirtualDevice*, VDC_Command*)
// from the VdiClient session management.
//
// It includes sqlvdi.h for the full type definitions — this is acceptable
// because this header is only included by .cpp files, never by any public
// header in include/.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

#include "sqlvdi.h"

// Result of processing a single VDI command.
enum class CommandResult {
    CONTINUE,       // Command completed normally; caller should call CompleteCommand
    CLOSE_REQUEST,  // VDC_Close or VD_E_CLOSE received; break out of command loop
    FAILED          // Unrecoverable error (sink failure, invalid protocol data)
};

// Convert a VDI command code to a human-readable string for logging.
// This is the only string-formatting function for VDI commands.
std::string command_code_string(unsigned long code);

// Validate that a VDC_Write command contains sane parameters before
// the command loop touches the buffer. Returns true if valid.
bool validate_write_command(const VDC_Command* cmd);

// Dispatch a single VDI command.
//
// Parameters:
//   device  - The virtual device that issued the command (for logging).
//   cmd     - The VDC_Command to process (nullptr is safe, returns FAILED).
//   result  - [out] The result of command processing.
//
// Returns: the command code (for caller decision-making).
// The caller uses 'result' to determine next action.
unsigned long dispatch_command(
    IClientVirtualDevice* device,
    VDC_Command* cmd,
    CommandResult& result);