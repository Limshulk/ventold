#pragma once
//
// vent public sdk.
// system base class.
// ——————————————————————
//
// base class all systems inherit from. defines the lifecycle contract:
// - name: unique identifier.
// - on_initialization: called during startup.
// - on_shutdown: called during shutdown.
//
// for additional capabilities, systems can implement role interfaces:
// - ir_client: system controls engine lifecycle (one per engine).
// - ir_bootstrap: system initializes early (internal only).
// - ir_dependencies: system declares dependencies.
// - ir_runnable: system has on_update() for main loop.

#include <_vent/vent_sdk.hpp>
#include <_vent/system/system_initialization_status.hpp>

#include <string_view>

namespace vent {

/// @brief base class for all engine systems.
/// systems must override name() and on_shutdown(). on_initialization() is
/// optionally overwritten, if needed.
class system_base {
public:
    virtual ~system_base() = default;

    /// @brief unique system name. convention: "vent.<system>".
    /// @return name string view.
    [[nodiscard]]
    virtual auto name() const -> std::string_view = 0;

    /// @brief initialization call.
    /// @return initialization result indicating success or failure.
    [[nodiscard]]
    virtual auto on_initialization() -> system_initialization_status {
        return system_initialization_status::success;
    }

    /// @brief shutdown call. clean up resources.
    virtual auto on_shutdown() -> void = 0;
};

}  // namespace vent
