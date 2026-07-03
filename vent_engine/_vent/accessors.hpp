#pragma once
//
// vent public sdk.
// core system accessors.
// ——————————————————————
//
// shortcut accessors for engine systems. implementations are provided by the
// engine library at link time.
// bootstrap systems get a additional convenience accessor that asserts if
// called before initialization completes.
//
// usage:
//   vent::job()->...;
//   vent::log()->...;
//   vent::system().get<ic_audio>()->...;

#include <_vent/interfaces/ic_system_registry.hpp>
#include <_vent/interfaces/ic_job.hpp>
#include <_vent/interfaces/ic_log.hpp>
#include <_vent/interfaces/ic_event_bus.hpp>
#include <_vent/interfaces/ic_platform.hpp>
#include <_vent/interfaces/ic_renderer.hpp>

namespace vent {

// --- system registry access ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the global system registry.
/// @return reference to the system registry.
VENT_API auto system() -> ic_system_registry&;

// --- core system shortcuts ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the job system.
/// @return pointer to the job system.
VENT_API auto job() -> ic_job*;

/// @brief get the log system.
/// @return pointer to the log system.
VENT_API auto log() -> ic_log*;

/// @brief get the event bus.
/// @return pointer to the event bus.
VENT_API auto event() -> ic_event_bus*;

/// @brief get the platform system.
/// @return pointer to the platform system, or nullptr if not available.
VENT_API auto platform() -> ic_platform*;

/// @brief get the renderer system.
/// @return pointer to the renderer system, or nullptr if not available.
VENT_API auto renderer() -> ic_renderer*;

// --- safe accessors (for initialization) ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the event bus if ready.
/// @return pointer to event bus, or nullptr if not ready.
VENT_API auto event_if_ready() -> ic_event_bus*;

/// @brief get the platform system if ready.
/// @return pointer to platform system, or nullptr if not ready.
VENT_API auto platform_if_ready() -> ic_platform*;

}  // namespace vent