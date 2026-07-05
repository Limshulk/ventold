#pragma once
//
// renderer module.
// render backend interface.
// ——————————————————————
//
// abstract interface for rendering backends (vulkan, dx12, etc.).
// the backend owns api-specific resources like surfaces and devices.

#include <_vent/vent_sdk.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/renderer/vertex.hpp>



#include <_vent/renderer/pipeline_desc.hpp>

#include <memory>
#include <string_view>
#include <span>

namespace vent {

// forward declarations.
class ic_window;
struct uniform_buffer_object;

class i_render_backend {
public:
    static constexpr std::string_view system_name =
        "vent.system.render_backend";

    virtual ~i_render_backend() = default;

    // --- backend info ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a friendly name for the backend api that implements this
    /// interface.
    /// @return a string view containing the backend api name.
    virtual auto get_api_name() const -> std::string_view = 0;

    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize the backend.
    /// @return true if successful, false otherwise.
    virtual auto initialize() -> bool = 0;

    /// @brief shutdown the backend and clean up all resources.
    virtual auto shutdown() -> void = 0;

    // --- surface management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a rendering surface and swapchain for the given window.
    /// @param window the window to create the surface for.
    /// @return true if successful, false otherwise.
    virtual auto create_surface(ic_window* window) -> bool = 0;

    /// @brief destroy the surface and swapchain associated with the window.
    /// @param window the window whose surface should be destroyed.
    virtual auto destroy_surface(ic_window* window) -> void = 0;

    // --- configuration ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the number of frames in flight for a specific window.
    /// this will trigger a swapchain recreation.
    /// @param window the window to configure.
    /// @param frames the number of frames in flight.
    virtual auto set_frames_in_flight(ic_window* window, u32 frames)
        -> void = 0;

    // --- render loop ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief acquire the next image from the swapchain to begin rendering.
    /// @return true if the frame was acquired successfully.
    virtual auto begin_frame(ic_window* window) -> bool = 0;

    /// @brief submit recorded commands and present the image to the screen.
    virtual auto end_frame(ic_window* window) -> void = 0;

    // --- pipeline management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a graphics pipeline from the given description.
    /// @param handle the frontend-generated handle for the pipeline.
    /// @param desc the pipeline descriptor.
    virtual auto create_graphics_pipeline(pipeline_handle handle, const pipeline_desc& desc) -> void = 0;

    /// @brief destroy a graphics pipeline.
    /// @param handle the pipeline handle to destroy.
    virtual auto destroy_graphics_pipeline(pipeline_handle handle) -> void = 0;

    /// @brief bind a graphics pipeline for the current frame.
    /// @param handle the pipeline handle to bind.
    virtual auto bind_pipeline(pipeline_handle handle) -> void = 0;

    /// @brief update global uniform buffer data.
    /// @param ubo the uniform data to pass to the renderer.
    virtual auto update_global_uniforms(const uniform_buffer_object& ubo) -> void = 0;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief creates a gpu mesh buffer from host vertex data.
    /// @param handle the frontend-generated handle for the mesh.
    /// @param vertices the list of vertices to upload.
    /// @param indices an optional list of indices for indexed drawing.
    virtual auto create_mesh(mesh_handle handle,
                             std::span<const vertex>   vertices,
                             std::span<const uint32_t> indices = {}) -> void = 0;

    /// @brief destroy a mesh.
    /// @param handle the mesh handle to destroy.
    virtual auto destroy_mesh(mesh_handle handle) -> void = 0;

    // --- rendering execution ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief records commands for a list of render packets on a background thread.
    /// @param chunk the chunk of draw commands to execute.
    /// @return an opaque handle to the recorded command list.
    virtual auto record_command_chunk(std::span<const render_packet> chunk)
        -> void* = 0;

    /// @brief executes the recorded command lists on the primary command buffer.
    /// @param command_lists span of opaque command list handles returned by record_command_chunk.
    virtual auto execute_recorded_commands(std::span<void* const> command_lists)
        -> void = 0;
};

}  // namespace vent