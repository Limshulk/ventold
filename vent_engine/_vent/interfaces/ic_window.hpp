#pragma once
//
// vent public sdk.
// client-facing window interface.
// ——————————————————————
//
// provides the client-facing interface for windows. windows are created via the
// platform system and owned by it. clients receive handles (ic_window*) to
// interact with windows.

#include <_vent/vent_sdk.hpp>

#include <string>
#include <string_view>

namespace vent {

// --- window_style ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief window style flags for customizing window appearance and behavior.
enum class window_style : u32 {
    none         = 0,
    resizable    = 1 << 0,  ///< window can be resized by the user.
    has_titlebar = 1 << 1,  ///< window has title bar and decorations.
    topmost      = 1 << 2,  ///< window stays above other windows.
    no_activate  = 1 << 3,  ///< don't take focus when shown.
    fullscreen   = 1 << 4,  ///< exclusive fullscreen mode.

    // common presets.
    standard   = resizable | has_titlebar,  ///< default windowed mode.
    borderless = none,                      ///< no decorations.
    popup      = topmost | no_activate,     ///< tooltip/popup style.
};

/// @brief bitwise OR for window_style.
[[nodiscard]]
constexpr auto operator|(window_style a, window_style b) -> window_style {
    return static_cast<window_style>(static_cast<u32>(a) | static_cast<u32>(b));
}
/// @brief bitwise AND for window_style.
[[nodiscard]]
constexpr auto operator&(window_style a, window_style b) -> window_style {
    return static_cast<window_style>(static_cast<u32>(a) & static_cast<u32>(b));
}
/// @brief check if a style flag is set.
[[nodiscard]]
constexpr auto has_style(window_style styles, window_style flag) -> bool {
    return (static_cast<u32>(styles) & static_cast<u32>(flag)) != 0;
}

// --- renderer_api ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief identifies which rendering api a window is configured for.
enum class renderer_api : u8 {
    none   = 0,  ///< no renderer (utility window).
    vulkan = 1,  ///< vulkan.
    opengl = 2,  ///< opengl.
    dx12   = 3,  ///< directx 12 (windows only).
};

// --- window_desc ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief descriptor for creating a window via platform()->create_window().
struct window_desc {
    std::string  title    = "vent window";  ///< window title.
    u32          width    = 1920;           ///< initial width in pixels.
    u32          height   = 1080;           ///< initial height in pixels.
    bool         centered = true;           ///< center window on screen.
    window_style style    = window_style::standard;  ///< window style flags.
    renderer_api renderer = renderer_api::vulkan;    ///< which renderer to use.
};

// --- ic_window ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief client-facing interface for a window. windows are created and owned
///        by the platform system. clients receive handles to interact with them.
class ic_window {
public:
    virtual ~ic_window() = default;

    // --- properties ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get the window title.
    [[nodiscard]]
    virtual auto get_title() const -> std::string_view = 0;

    /// @brief get the window width in screen coordinates.
    [[nodiscard]]
    virtual auto get_width() const -> u32 = 0;

    /// @brief get the window height in screen coordinates.
    [[nodiscard]]
    virtual auto get_height() const -> u32 = 0;

    /// @brief get the framebuffer width in pixels (may differ on hidpi).
    [[nodiscard]]
    virtual auto get_framebuffer_width() const -> u32 = 0;

    /// @brief get the framebuffer height in pixels (may differ on hidpi).
    [[nodiscard]]
    virtual auto get_framebuffer_height() const -> u32 = 0;

    /// @brief check if this is the main (first-created) window.
    [[nodiscard]]
    virtual auto is_main() const -> bool = 0;

    /// @brief check if this window has input focus.
    [[nodiscard]]
    virtual auto has_focus() const -> bool = 0;

    /// @brief check if a close was requested (user clicked X, etc.).
    [[nodiscard]]
    virtual auto should_close() const -> bool = 0;

    /// @brief get the window style flags.
    [[nodiscard]]
    virtual auto get_style() const -> window_style = 0;

    /// @brief get the renderer api this window was created for.
    [[nodiscard]]
    virtual auto get_renderer() const -> renderer_api = 0;

    // --- actions ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the window title.
    virtual auto set_title(std::string_view title) -> void = 0;

    /// @brief resize the window to new dimensions.
    virtual auto resize(u32 width, u32 height) -> void = 0;

    /// @brief show the window (windows are created hidden).
    virtual auto show() -> void = 0;

    /// @brief hide the window.
    virtual auto hide() -> void = 0;

    /// @brief request the window to close (sets should_close flag).
    virtual auto request_close() -> void = 0;

    /// @brief clear the close request flag (to cancel a close).
    virtual auto clear_close_request() -> void = 0;

    // --- event polling ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief poll for size changes since last call.
    /// @param out_width receives the new width if changed.
    /// @param out_height receives the new height if changed.
    /// @return true if size changed, false otherwise.
    virtual auto poll_size_changed(u32& out_width, u32& out_height) -> bool = 0;

    /// @brief poll for focus changes since last call.
    /// @param out_has_focus receives the new focus state if changed.
    /// @return true if focus changed, false otherwise.
    virtual auto poll_focus_changed(bool& out_has_focus) -> bool = 0;

    // --- native access ---
    // —————————————————————————————————————————————————————————————————————————
    // for renderer integration.

    /// @brief get the native window handle.
    /// @return platform-specific: HWND on Windows, X11 Window/wl_surface on
    /// Linux.
    [[nodiscard]]
    virtual auto get_native_handle() const -> void* = 0;

    /// @brief get the native display/instance handle.
    /// @return platform-specific: HINSTANCE on Windows, Display*/wl_display on
    /// Linux.
    [[nodiscard]]
    virtual auto get_native_display() const -> void* = 0;
};

}  // namespace vent