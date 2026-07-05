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

#include <_vent/asset/ic_asset.hpp>
#include <_vent/event_bus/ic_event_bus.hpp>
#include <_vent/core/ic_system_registry.hpp>
#include <_vent/job/ic_job.hpp>
#include <_vent/log/ic_log.hpp>
#include <_vent/platform/ic_platform.hpp>
#include <_vent/renderer/ic_renderer.hpp>
#include <_vent/world/ic_world.hpp>

namespace vent {

class ic_renderer;

// --- system registry access ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the global system registry.
/// @return reference to the system registry.
VENT_API auto system() -> ic_system_registry&;

// --- core system shortcuts ---
// —————————————————————————————————————————————————————————————————————————————

VENT_API auto asset() -> ic_asset*;

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

/// @brief get the world system.
/// @return pointer to the world system.
VENT_API auto world() -> ic_world*;

// --- safe accessors (for initialization) ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the event bus if ready.
/// @return pointer to event bus, or nullptr if not ready.
VENT_API auto event_if_ready() -> ic_event_bus*;

/// @brief get the platform system if ready.
/// @return pointer to platform system, or nullptr if not ready.
VENT_API auto platform_if_ready() -> ic_platform*;

}  // namespace vent