#pragma once
//
// platform module.
// engine-facing window interface.
// ——————————————————————
//
// extends ic_window with internal engine operations.

#include <_vent/interfaces/ic_window.hpp>

namespace vent {

// forward declarations.
// class i_input;

/// @brief internal interface for windows. extends ic_window with operations
///        only used by engine internals (not exposed to clients).
class i_window : public ic_window {
public:
    virtual ~i_window() = default;

    /// @brief set whether this window is the main window.
    /// @param is_main true to mark as main window.
    virtual auto set_main(bool is_main) -> void = 0;

    /// @brief attach an input system for event routing.
    /// @param input pointer to input system (can be nullptr to detach).
    // virtual auto set_input(i_input* input) -> void = 0;
};

}  // namespace vent
