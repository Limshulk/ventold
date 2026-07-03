#pragma once
//
// renderer module.
// render backend interface.
// ——————————————————————
//
// abstract interface for rendering backends (vulkan, dx12, etc.).
// the backend owns api-specific resources like surfaces and devices.

#include <string_view>

namespace vent {

// forward declarations.
class ic_window;

class i_render_backend {
public:
    virtual ~i_render_backend() = default;

    // --- backend info ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a friendly name for the backend api that implements this
    /// interface.
    /// @return a string view containing the backend api name.
    virtual auto get_api_name() const -> std::string_view = 0;

    // --- surface management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a rendering surface for the given window.
    /// @param window the window to create a surface for.
    /// @return true on success, false on failure.
    virtual auto create_surface(ic_window* window) -> bool = 0;

    /// @brief destroy the rendering surface associated with the given window.
    /// @param window the window whose surface should be destroyed.
    virtual auto destroy_surface(ic_window* window) -> void = 0;

    // --- render loop ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set frames in flight for a given window's swapchain.
    virtual auto set_frames_in_flight(ic_window* window, u32 count) -> void = 0;

    /// @brief acquire the next image from the swapchain to begin rendering.
    virtual auto begin_frame(ic_window* window) -> bool = 0;

    /// @brief submit recorded commands and present the image to the screen.
    virtual auto end_frame(ic_window* window) -> void = 0;
};

}  // namespace vent