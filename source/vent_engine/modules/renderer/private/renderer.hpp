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

    auto begin_frame(ic_window* window) -> bool override;
    auto end_frame(ic_window* window) -> void override;

    auto set_camera(const math::mat4& view, const math::mat4& proj)
        -> void override;

private:
    struct cached_model {
        model_asset* asset  = nullptr;
        mesh_handle  mesh   = INVALID_MESH_HANDLE;
        bool         failed = false;  ///< true if a load attempt failed; skip
                                      ///< re-loading it every frame.
    };

    struct cached_texture {
        image_asset*   asset   = nullptr;
        texture_handle texture = INVALID_TEXTURE_HANDLE;
        bool           failed  = false;  ///< true if a load attempt failed.
    };

    std::unordered_map<std::string, cached_model>   _model_cache;
    std::unordered_map<std::string, cached_texture> _texture_cache;
    std::mutex                                      _model_mutex;
    std::mutex                                      _texture_mutex;

    pipeline_handle _default_pipeline = INVALID_PIPELINE_HANDLE;
    shader_asset*   _default_shader   = nullptr;

    math::mat4 _view_matrix = math::mat4::identity();
    math::mat4 _proj_matrix = math::mat4::identity();

    command_list _command_list;

private:
    // --- targets ---
    // —————————————————————————————————————————————————————————————————————————

    i_render_backend* _backend = nullptr;    ///< backend from system_registry.
    std::vector<ic_window*> _windows;        ///< target windows for rendering.
    std::mutex              _windows_mutex;  ///< protects window collection.
    subscription_id         _window_sub {};  ///< window.created subscription.
    subscription_id         _window_destroyed_sub {};  ///< window.destroyed
                                                       ///< subscription.
    std::atomic<u64> _next_pipeline_handle {1};
    std::atomic<u64> _next_mesh_handle {1};
    std::atomic<u64> _next_texture_handle {1};


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