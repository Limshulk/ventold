#pragma once
//
// vent public sdk.
// system initialization status.
// ——————————————————————
//
// return type for system initialization indicating success or failure.

namespace vent {

/// @brief result from a system initialization step.
enum class system_initialization_status {
    success,  ///< initialization succeeded.
    failed    ///< initialization failed.
};

}  // namespace vent
