#pragma once
//
// renderer module.
// render backend interface.
// ——————————————————————
//
// abstract interface for rendering backends (vulkan, dx12, etc.).
// the backend owns api-specific resources like surfaces and devices.

#include <_vent/vent_sdk.hpp>
#include <_vent/renderer/pipeline_desc.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/renderer/texture_desc.hpp>
#include <_vent/renderer/vertex.hpp>

#include <span>
#include <string_view>
#include <vector>

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

    /// @brief waits for all backend operations to complete and returns
    /// afterwards.
    virtual auto wait_for_idle() -> void = 0;

    // --- surface management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a rendering surface and swapchain for the given window.
    /// @param window the window to create the surface for.
    /// @return true if successful, false otherwise.
    virtual auto create_surface(ic_window* window) -> bool = 0;

    /// @brief destroy the surface and swapchain associated with the window.
    /// @param window the window whose surface should be destroyed.
    virtual auto destroy_surface(ic_window* window) -> void = 0;

    /// @brief check whether a live (not destruction-marked) surface exists for
    /// the given window. used by the frontend's per-frame reconcile pass.
    [[nodiscard]]
    virtual auto has_surface(const ic_window* window) const -> bool = 0;

    /// @brief get the windows of all live surfaces. used by the frontend to
    /// detect surfaces whose window no longer exists (reconcile pass).
    /// @note the returned pointers are only compared for identity, never
    /// dereferenced — a window may already be destroyed.
    [[nodiscard]]
    virtual auto get_surface_windows() const -> std::vector<ic_window*> = 0;

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
    virtual auto create_graphics_pipeline(pipeline_handle      handle,
                                          const pipeline_desc& desc)
        -> void = 0;

    /// @brief destroy a graphics pipeline.
    /// @param handle the pipeline handle to destroy.
    virtual auto destroy_graphics_pipeline(pipeline_handle handle) -> void = 0;

    // --- textures ---
    virtual auto create_texture(texture_handle handle, const texture_desc& desc)
        -> void                                                 = 0;
    virtual auto destroy_texture(texture_handle handle) -> void = 0;

    /// @brief update the per-frame uniform data (camera matrices) for the
    /// window currently between begin_frame and end_frame.
    /// @note must be called after begin_frame: the uniform ring slot belongs
    /// to that window's swapchain and is only provably free once its frame
    /// fence has been waited on (which begin_frame does). this is what makes
    /// per-window cameras race-free — see "which fence proves this memory is
    /// free?" in the threading notes.
    /// @param ubo the uniform data for this window's current frame.
    virtual auto update_frame_uniforms(const uniform_buffer_object& ubo)
        -> void = 0;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief creates a gpu mesh buffer from host vertex data.
    /// @param handle the frontend-generated handle for the mesh.
    /// @param vertices the list of vertices to upload.
    /// @param indices an optional list of indices for indexed drawing.
    virtual auto create_mesh(mesh_handle               handle,
                             std::span<const vertex>   vertices,
                             std::span<const uint32_t> indices = {})
        -> void = 0;

    /// @brief destroy a mesh.
    /// @param handle the mesh handle to destroy.
    virtual auto destroy_mesh(mesh_handle handle) -> void = 0;

    // --- rendering execution ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief records commands for a list of render packets on a background
    /// thread.
    /// @param chunk the chunk of draw commands to execute.
    /// @return an opaque handle to the recorded command list.
    virtual auto record_command_chunk(std::span<const render_packet> chunk)
        -> void* = 0;

    /// @brief executes the recorded command lists on the primary command buffer.
    /// @param command_lists span of opaque command list handles returned by
    /// record_command_chunk.
    virtual auto execute_recorded_commands(std::span<void* const> command_lists)
        -> void = 0;
};

}  // namespace vent