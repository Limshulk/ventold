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
#include <_vent/renderer/vertex.hpp>
#include <_vent/renderer/render_command.hpp>

#include <memory>
#include <span>

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
    virtual auto create_graphics_pipeline(const pipeline_desc& desc)
        -> std::unique_ptr<ic_pipeline> = 0;

    /// @brief bind a graphics pipeline for the current frame.
    /// @param pipeline the pipeline to bind.
    virtual auto bind_pipeline(ic_pipeline* pipeline) -> void = 0;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a mesh from a list of vertices.
    /// @param vertices the raw vertex data.
    /// @return an opaque handle to the mesh.
    virtual auto create_mesh(std::span<const vertex> vertices)
        -> mesh_handle = 0;

    // --- rendering submission ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a thread-local command list for the current worker thread.
    /// this allows multiple threads to safely record draw commands in parallel.
    virtual auto get_command_list() -> command_list& = 0;

    /// @brief submit all recorded command lists to the renderer.
    /// usually called once per frame by the main thread.
    virtual auto submit_command_lists(std::span<command_list* const> lists) -> void = 0;
};

}  // namespace vent
