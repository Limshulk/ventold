#pragma once
//
// renderer module.
// renderer frontend.
// ——————————————————————
//
// implements the renderer frontend inheriting i_renderer. the frontend
// processes all rendering.

#include <renderer/interfaces/i_renderer.hpp>
#include <renderer/interfaces/i_render_backend.hpp>

#include <_vent/accessors.hpp>
#include <_vent/core/ir_dependencies.hpp>
#include <_vent/platform/ic_platform.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/system/system_base.hpp>

#include <atomic>
#include <mutex>

namespace vent {

// --- forward declarations ---
class i_device;
class i_window;

class renderer_system final
    : public system_base
    , public i_renderer
    , public ir_dependencies {
public:
    renderer_system()           = default;
    ~renderer_system() override = default;

    VENT_NO_COPY_MOVE(renderer_system);

public:
    // --- system_base implementations ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_renderer::system_name;
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override {
        return initialize() ? system_initialization_status::success
                            : system_initialization_status::failed;
    }

    auto on_shutdown() -> void override { shutdown(); }

    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {ic_platform::system_name};
        return deps;
    }

    // --- ic_renderer implementation ---
    // —————————————————————————————————————————————————————————————————————————

    auto set_frames_in_flight(ic_window* window, u32 frames) -> void override;
    auto begin_frame(ic_window* window) -> bool override;
    auto end_frame(ic_window* window) -> void override;

    auto create_graphics_pipeline(const pipeline_desc& desc)
        -> pipeline_handle override;

    auto destroy_graphics_pipeline(pipeline_handle handle) -> void override;

    auto bind_pipeline(pipeline_handle handle) -> void override;

    auto update_global_uniforms(const uniform_buffer_object& ubo) -> void override;

    auto create_mesh(std::span<const vertex>   vertices,
                     std::span<const uint32_t> indices = {})
        -> mesh_handle override;

    auto destroy_mesh(mesh_handle handle) -> void override;

    auto get_command_list() -> command_list& override;
    auto submit_command_lists(std::span<command_list* const> lists)
        -> void override;

private:
    // --- targets ---
    // —————————————————————————————————————————————————————————————————————————

    i_render_backend* _backend = nullptr;    ///< backend from system_registry.
    std::vector<ic_window*> _windows;        ///< target windows for rendering.
    std::mutex              _windows_mutex;  ///< protects window collection.
    subscription_id         _window_sub {};  ///< window.created subscription.
    subscription_id         _window_destroyed_sub {};  ///< window.destroyed
                                                       ///< subscription.
    std::atomic<mesh_handle>     _next_mesh_handle {1};
    std::atomic<pipeline_handle> _next_pipeline_handle {1};
    i_device* _device = nullptr;                       ///< gpu device.


private:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    // --- initialization stages ---

    /// @brief initialize the renderer.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief shutdown renderer system.
    auto shutdown() -> void;
};

}  // namespace vent