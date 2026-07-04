#pragma once
//
// vent public sdk.
// role interface: client.
// ——————————————————————
//
// the client role identifies the main application controlling the engine
// lifecycle. exactly one system per engine instance should implement this role.
//
// the engine queries this interface to determine when to exit the main loop.

#include <_vent/vent_sdk.hpp>
#include <_vent/platform/ic_window.hpp>

namespace vent {

/// @brief role interface identifying the main application.
class ir_client {
public:
    virtual ~ir_client() = default;

    /// @brief check if the application wants to continue running.
    /// @return true to continue main loop, false to exit.
    /// @note must be thread-safe. may be called from any thread.
    [[nodiscard]]
    virtual auto is_running() const -> bool = 0;

    /// @brief request the application to exit the main loop.
    /// @note must be thread-safe. may be called from any thread.
    virtual auto request_exit() -> void = 0;

    /// @brief select how the engine should react when the main window is
    /// closed.
    /// @note may be queried from the main loop thread.
    [[nodiscard]]
    virtual auto close_policy() const -> window_close_policy {
        return window_close_policy::exit_on_main_close;
    }
};

}  // namespace vent
