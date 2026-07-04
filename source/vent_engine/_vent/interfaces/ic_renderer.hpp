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
#include <memory>

namespace vent {

// forward declarations
class ic_window;
class ic_pipeline;
struct pipeline_desc;

class ic_renderer {
public:
    static constexpr std::string_view system_name = "vent.system.renderer";

protected:
    ~ic_renderer() = default;

public:
    // --- configuration ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the number of frames in flight for a specific window.
    /// this will trigger a swapchain recreation on the backend.
    /// @param window the window to configure.
    /// @param frames the number of frames in flight (typically 2 or 3).
    virtual auto set_frames_in_flight(ic_window* window, u32 frames)
        -> void = 0;

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

    // --- pipeline management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a graphics pipeline from the given description.
    virtual auto create_graphics_pipeline(const pipeline_desc& desc) -> std::unique_ptr<ic_pipeline> = 0;
};

}  // namespace vent
