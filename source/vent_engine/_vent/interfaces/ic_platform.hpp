#pragma once
//
// vent public sdk.
// client-facing platform interface.
// ——————————————————————
//
// provides client access to the platform system for creating windows and
// polling events. the platform system abstracts os-specific functionality.

#include <_vent/vent_sdk.hpp>
#include <_vent/interfaces/ic_window.hpp>
#include <_vent/interfaces/ir_client.hpp>  // TODO: needed for window_close_policy. check if we can move the definition somewhere else.

namespace vent {

/// @brief window manager backend type. required mainly for rendering to create
/// the correct surface type.
enum class platform_type {
    unknown = 0,  ///< you'll die if this happens.
    x11     = 1,  ///< good linux.
    wayland = 2,  ///< broken linux.
    win32   = 3,  ///< ai slop.
    cocoa   = 4,  ///< heiße schokolade.
};

class ic_platform {
public:
    static constexpr std::string_view system_name = "vent.system.platform";

    virtual ~ic_platform() = default;

    // --- platform os backend ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    virtual auto get_platform_type() const -> platform_type = 0;

    /// @brief get the window system extensions required for a specific graphics
    /// api. may be unused or return a null vector if not supported by the api.
    /// @param api the graphics api to query extensions for.
    /// @return list of required extension names.
    [[nodiscard]]
    virtual auto get_window_system_extensions(renderer_api api) const
        -> std::vector<const char*> = 0;

    // --- window management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a new window.
    /// @param desc window creation descriptor.
    /// @return handle to the new window, or nullptr on failure.
    /// @note windows are created hidden; call show() to display.
    /// @note the first window created becomes the main window.
    [[nodiscard]]
    virtual auto create_window(const window_desc& desc) -> ic_window* = 0;

    /// @brief destroy a window.
    /// @param window handle to the window to destroy.
    /// @note destroying the main window typically signals app shutdown.
    virtual auto destroy_window(ic_window* window) -> void = 0;

    /// @brief get the main window.
    /// @return handle to the main window, or nullptr if none exists.
    [[nodiscard]]
    virtual auto get_main_window() const -> ic_window* = 0;

    /// @brief get the number of active windows.
    [[nodiscard]]
    virtual auto get_window_count() const -> u32 = 0;

    /// @brief get all active windows.
    [[nodiscard]]
    virtual auto get_windows() const -> std::vector<ic_window*> = 0;

    /// @brief set the policy used when the main window receives a close
    /// request.
    virtual auto set_close_policy(window_close_policy policy) -> void = 0;

    // --- event loop ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief poll and dispatch window/input events.
    /// @note call this once per frame before processing input.
    virtual auto poll_events() -> void = 0;
};

}  // namespace vent