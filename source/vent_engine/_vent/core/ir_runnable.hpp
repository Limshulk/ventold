#pragma once
//
// vent public sdk.
// role interface: runnable.
// ——————————————————————
//
// systems that implement this role participate in the main loop and receive
// per-frame update calls. use this for systems that need continuous processing.
//
// ordering: runnables execute in ascending run_phase() order. within the same
// phase, registration order is preserved (deterministic, but unspecified — do
// not depend on it). this makes the frame a pipeline with named stages:
// simulate (gameplay mutates the world) → render (world is read-only, the
// renderer extracts and submits). encoding the order as sortable data instead
// of hardcoding it in the loop means adding a new phase later (input, physics,
// extraction, ...) is a one-line change, not a loop rewrite.

#include <_vent/vent_sdk.hpp>

namespace vent {

// --- frame phases ---
// —————————————————————————————————————————————————————————————————————————————
// deliberate gaps between values leave room for future phases (input, physics,
// extraction, post-render, ...) without renumbering existing systems.

/// @brief default phase: gameplay / simulation. the world may be mutated here.
inline constexpr i32 run_phase_simulate = 0;

/// @brief render phase: runs after simulation. the world is read-only by
/// contract — the renderer extracts a snapshot and submits gpu work.
inline constexpr i32 run_phase_render = 1000;

/// @brief role interface for systems that participate in the main loop.
class ir_runnable {
public:
    virtual ~ir_runnable() = default;

    /// @brief called every frame from the main loop.
    /// @param delta_time time since last frame in seconds.
    virtual auto on_update(f64 delta_time) -> void = 0;

    /// @brief phase of the frame this runnable executes in. lower runs
    /// earlier. defaults to the simulate phase (clients, gameplay systems).
    [[nodiscard]]
    virtual auto run_phase() const -> i32 {
        return run_phase_simulate;
    }
};

}  // namespace vent
