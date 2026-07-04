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

#include <renderer/interfaces/i_pipeline.hpp>

#include <memory>
#include <string_view>
#include <span>

namespace vent {

// forward declarations.
class ic_window;

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
    virtual auto create_graphics_pipeline(const pipeline_desc& desc)
        -> std::unique_ptr<i_pipeline> = 0;

    /// @brief bind a graphics pipeline for the current frame.
    /// @param pipeline the pipeline to bind.
    virtual auto bind_pipeline(ic_pipeline* pipeline) -> void = 0;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    virtual auto create_mesh(
        std::span<const vertex>   vertices,
        std::span<const uint32_t> indices = {}) -> mesh_handle = 0;

    // --- rendering execution ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief execute a fully sorted array of render packets.
    /// @param packets the perfectly sorted array of packets to render this frame.
    virtual auto execute_packets(std::span<const render_packet> packets)
        -> void = 0;
};

}  // namespace vent