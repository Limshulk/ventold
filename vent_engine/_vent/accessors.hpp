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

#include <_vent/interfaces/ic_log.hpp>

namespace vent {

// --- core system shortcuts ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the log system.
/// @return ptr to log system. either real one or fallback.
VENT_API auto log() -> ic_log*;

}  // namespace vent