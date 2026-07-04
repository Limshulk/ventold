#pragma once
//
// vent public sdk.
// role interface: runnable.
// ——————————————————————
//
// systems that implement this role participate in the main loop and receive
// per-frame update calls. use this for systems that need continuous processing.
//
// note: runnables are called in undefined order. if ordering matters,
// coordinate via events or consider system dependencies.

#include <_vent/vent_sdk.hpp>

namespace vent {

/// @brief role interface for systems that participate in the main loop.
class ir_runnable {
public:
    virtual ~ir_runnable() = default;

    /// @brief called every frame from the main loop.
    /// @param delta_time time since last frame in seconds.
    virtual auto on_update(f64 delta_time) -> void = 0;
};

}  // namespace vent
