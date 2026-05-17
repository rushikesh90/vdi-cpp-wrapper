#include "vdi_protocol.h"
#include "logging.h"

// We need the full VDI type definitions here, but NOT in the public header.
#include "sqlvdi.h"

std::string command_code_string(unsigned long code) {
    switch (code) {
    case VDC_Read:       return "VDC_Read";
    case VDC_Write:      return "VDC_Write";
    case VDC_Flush:      return "VDC_Flush";
    case VDC_Close:      return "VDC_Close";
    case VDC_ClearError: return "VDC_ClearError";
    default:             return "UNKNOWN(" + std::to_string(code) + ")";
    }
}

bool validate_write_command(const VDC_Command* cmd) {
    // Never trust protocol data blindly.
    //
    // Validate:
    //   - Non-null buffer pointer
    //   - Sane size (> 0, reasonable upper bound)
    //   - Reasonable position offset

    if (!cmd) {
        LOG_ERROR("validate_write_command: cmd is null");
        return false;
    }

    if (!cmd->buffer) {
        LOG_ERROR("validate_write_command: buffer pointer is null");
        return false;
    }

    if (cmd->size == 0) {
        LOG_ERROR("validate_write_command: chunk size is 0");
        return false;
    }

    // 1 GB upper bound — VDI protocol typically uses 1 MB max transfer size,
    // but we set a generous safety limit to catch corrupt protocol data.
    constexpr DWORD MAX_SANE_CHUNK_SIZE = 1024 * 1024 * 1024;  // 1 GB
    if (cmd->size > MAX_SANE_CHUNK_SIZE) {
        LOG_ERROR("validate_write_command: chunk size %lu exceeds sanity limit %lu",
                   cmd->size, MAX_SANE_CHUNK_SIZE);
        return false;
    }

    return true;
}

unsigned long dispatch_command(
    IClientVirtualDevice* device,
    VDC_Command* cmd,
    CommandResult& result)
{
    // Default: continue (caller should call CompleteCommand)
    result = CommandResult::CONTINUE;

    if (!cmd) {
        LOG_ERROR("dispatch_command: cmd is null");
        result = CommandResult::FAILED;
        return 0;
    }

    switch (cmd->commandCode) {

    case VDC_Read:
        LOG_DEBUG("  [VDC_Read]");
        // SQL Server requesting data read (not expected during pure BACKUP)
        break;

    case VDC_Write: {
        LOG_DEBUG("[WRITE] size=%zu", cmd->size);
        // Actual sink write and metrics recording happen in the command loop.
        // Here we only do dispatch-level work (validation done by caller).
        break;
    }

    case VDC_Flush:
        LOG_DEBUG("  [VDC_Flush]");
        // Sink flush is handled by the command loop (needs sink reference)
        break;

    case VDC_ClearError:
        LOG_DEBUG("  [VDC_ClearError]");
        break;

    case VDC_Close:
        LOG_INFO("  [VDC_Close]");
        result = CommandResult::CLOSE_REQUEST;
        break;

    default:
        LOG_DEBUG("  [UNKNOWN COMMAND: %lu]", cmd->commandCode);
        break;
    }

    return cmd->commandCode;
}