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

namespace vent {

// TODO: check if we can move this somewhere else.
/// @brief policy used when the main window receives a close request.
enum class window_close_policy {
    exit_on_main_close = 0,  ///< closing main window exits the app.
    keep_running_until_all_closed =
        1,  ///< main window close request is ignored until all windows are
            ///< closed.
};

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
