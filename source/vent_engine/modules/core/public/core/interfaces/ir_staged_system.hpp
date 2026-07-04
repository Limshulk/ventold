#pragma once
//
// core module.
// staged system interface.
// ——————————————————————
//
// role interface for internal systems that require staged, asynchronous
// initialization.
// once this role is implemented into a system, on_staged_initialization() must
// overwritten. system_creator detects this automatically and calls
// on_staged_initialization(). the method on_initialization(), obtained by
// system_base, is ignored if this role is implemented and will not be executed.

#include <_vent/vent_sdk.hpp>
#include <core/system/system_init_command.hpp>

namespace vent {

/// @brief role interface for systems requiring staged initialization.
class ir_staged_system {
public:
    virtual ~ir_staged_system() = default;

    /// @brief initialization call for staged systems.
    /// @param stage current initialization stage (0-based).
    /// @return command indicating next action.
    [[nodiscard]]
    virtual auto on_staged_initialization(i32 stage) -> system_init_command = 0;
};

}  // namespace vent
