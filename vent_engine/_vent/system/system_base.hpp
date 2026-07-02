#pragma once
//
// vent public sdk.
// system base class.
// ——————————————————————
//
// base class all systems inherit from. defines the lifecycle contract:
// - name: unique identifier.
// - on_initialization: called during startup (may be staged).
// - on_shutdown: called during shutdown.
//
// for additional capabilities, systems can implement role interfaces:
// - ir_client: system controls engine lifecycle (one per engine).
// - ir_bootstrap: system initializes early (internal only).
// - ir_dependencies: system declares dependencies.
// - ir_runnable: system has on_update() for main loop.

#include <_vent/vent_sdk.hpp>
#include <_vent/system/initialization_result.hpp>

#include <string_view>

namespace vent {

/// @brief base class for all engine systems.
/// systems must override name(), on_initialization(), and on_shutdown().
class system_base {
public:
    virtual ~system_base() = default;

    /// @brief unique system name. convention: "vent.<system>".
    /// @return name string view.
    [[nodiscard]]
    virtual auto name() const -> std::string_view = 0;

    /// @brief initialization call. may be called multiple times for staged
    /// initialization.
    /// @param stage current initialization stage (0-based).
    /// @return initialization result indicating next action.
    [[nodiscard]]
    virtual auto on_initialization(i32 stage = 0)
        -> initialization_result = 0;

    /// @brief shutdown call. clean up resources.
    virtual auto on_shutdown() -> void = 0;
};

}  // namespace vent
