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
#include <_vent/renderer/pipeline_desc.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/renderer/texture_desc.hpp>

#include <memory>
#include <span>

namespace vent {

// forward declarations
class ic_window;

struct uniform_buffer_object;

class ic_renderer {
public:
    virtual ~ic_renderer() = default;

    static constexpr std::string_view system_name = "vent.system.renderer";
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
        -> pipeline_handle = 0;

    /// @brief destroy a graphics pipeline.
    /// @param handle the pipeline to destroy.
    virtual auto destroy_graphics_pipeline(pipeline_handle handle) -> void = 0;

    /// @brief bind a graphics pipeline for the current frame.
    /// @param handle the pipeline handle to bind.
    virtual auto bind_pipeline(pipeline_handle handle) -> void = 0;

    /// @brief update global uniform buffer data.
    /// @param ubo the uniform data to pass to the renderer.
    virtual auto update_global_uniforms(const uniform_buffer_object& ubo)
        -> void = 0;

    // --- textures ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a texture from declarative description.
    virtual auto create_texture(const texture_desc& desc) -> texture_handle = 0;

    /// @brief destroy a texture.
    virtual auto destroy_texture(texture_handle handle) -> void = 0;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a mesh from a list of vertices.
    /// @param vertices the raw vertex data.
    /// @param indices the raw index data.
    /// @return an opaque handle to the mesh.
    virtual auto create_mesh(std::span<const vertex>   vertices,
                             std::span<const uint32_t> indices = {})
        -> mesh_handle = 0;

    /// @brief destroy a mesh.
    /// @param handle the mesh to destroy.
    virtual auto destroy_mesh(mesh_handle handle) -> void = 0;

    // --- rendering submission ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a thread-local command list for the current worker thread.
    /// this allows multiple threads to safely record draw commands in parallel.
    virtual auto get_command_list() -> command_list& = 0;

    /// @brief submit all recorded command lists to the renderer.
    /// usually called once per frame by the main thread.
    virtual auto submit_command_lists(std::span<command_list* const> lists)
        -> void = 0;
};

}  // namespace vent
