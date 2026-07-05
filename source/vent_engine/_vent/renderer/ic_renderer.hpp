#pragma once
//
// vent public sdk.
// renderer module interface.
// ——————————————————————
//
// abstract interface for the global renderer system.
// provides high-level control over rendering configuration and frame lifecycle
// without exposing backend-specific details (like vulkan or directx).

#include <_vent/vent_sdk.hpp>
#include <_vent/renderer/pipeline_desc.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/renderer/texture_desc.hpp>
#include <_vent/renderer/vertex.hpp>

namespace vent {

// forward declarations
class ic_window;

struct uniform_buffer_object;

class ic_renderer {
public:
    virtual ~ic_renderer() = default;

    static constexpr std::string_view system_name = "vent.system.renderer";

    // --- render loop ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief acquire the next image from the swapchain to begin rendering.
    /// @param window the window to begin rendering to.
    /// @return true if the frame was acquired successfully. false if the window
    /// is minimized, out of date, or not ready.
    virtual auto begin_frame(ic_window* window) -> bool = 0;

    /// @brief submit recorded commands and present the image to the screen.
    /// @param window the window to end rendering for.
    virtual auto end_frame(ic_window* window) -> void = 0;

    // --- camera management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the global camera matrices for the current frame.
    /// @param view the view matrix.
    /// @param proj the projection matrix.
    virtual auto set_camera(const math::mat4& view, const math::mat4& proj)
        -> void = 0;
};

}  // namespace vent
