#pragma once
//
// renderer module.
// renderer frontend.
// ——————————————————————
//
// implements the renderer frontend inheriting i_renderer. the frontend owns
// the render phase of the frame: it runs as an ir_runnable (after the client's
// simulate phase), reconciles surfaces with the platform's window list,
// extracts the world into a command list ONCE per frame, and replays it per
// window. the client never issues render calls — it describes the scene via
// world components (mesh, transform, camera) and the engine does the rest.

#include <renderer/interfaces/i_renderer.hpp>
#include <renderer/interfaces/i_render_backend.hpp>

#include <_vent/accessors.hpp>
#include <_vent/asset/image.hpp>
#include <_vent/asset/model.hpp>
#include <_vent/asset/shader.hpp>
#include <_vent/core/ir_dependencies.hpp>
#include <_vent/core/ir_runnable.hpp>
#include <_vent/platform/ic_platform.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/system/system_base.hpp>
#include <_vent/world/ic_world.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vent {

class renderer_system final
    : public system_base
    , public i_renderer
    , public ir_dependencies
    , public ir_runnable {
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

    // --- ir_runnable implementation ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief the engine's render tick: reconcile surfaces, extract the world
    /// once, then render every window with a surface.
    auto on_update(f64 delta_time) -> void override;

    /// @brief the renderer runs in the render phase, after all simulate-phase
    /// runnables (client, gameplay). the world is read-only by contract here.
    [[nodiscard]]
    auto run_phase() const -> i32 override {
        return run_phase_render;
    }

private:
    // --- asset caches ---
    // —————————————————————————————————————————————————————————————————————————
    // string-keyed for now; asset handles replace this in phase 2 (roadmap 2.1).

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
    bool            _pipeline_failed  = false;  ///< default pipeline creation
                                                ///< failed; logged once, don't
                                                ///< retry every frame.

    /// @brief placeholder texture handle (magenta/black checkerboard),
    /// generated procedurally at first frame — no file i/o, can't be missing.
    /// phase 2's async pipeline will use it as the "still loading" visual.
    texture_handle _placeholder_texture = INVALID_TEXTURE_HANDLE;

    // --- frame snapshot ---
    // —————————————————————————————————————————————————————————————————————————
    // extraction output: everything the per-window render stage touches is
    // copied BY VALUE here, once per frame. this is the world/render seam —
    // render never reads live world state after extract_frame() returns,
    // which is what later allows simulation and rendering to overlap.

    /// @brief per-window camera snapshot, resolved during extraction.
    struct window_camera {
        ic_window* window = nullptr;  ///< the target window (identity only
                                      ///< until render_window dereferences it;
                                      ///< windows with surfaces are alive for
                                      ///< the frame by the reconcile contract).
        math::mat4       pose = math::mat4::identity();  ///< camera-to-world.
        camera_component camera {};                      ///< projection params.
    };

    command_list               _command_list;     ///< extracted draw packets.
    std::vector<window_camera> _window_cameras;   ///< per-window camera data.
    bool _warned_no_camera = false;  ///< fallback-camera warning, logged once.

    // --- targets ---
    // —————————————————————————————————————————————————————————————————————————

    i_render_backend* _backend = nullptr;  ///< backend from system_registry.

    std::atomic<u64> _next_pipeline_handle {1};
    std::atomic<u64> _next_mesh_handle {1};
    std::atomic<u64> _next_texture_handle {1};

private:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize the renderer.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief shutdown renderer system.
    auto shutdown() -> void;

    // --- frame stages ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create surfaces for platform windows that lack one and destroy
    /// surfaces whose window is gone. runs at a known-safe point (frame start,
    /// main thread) instead of on job-worker event callbacks — deterministic,
    /// and immune to the platform-thread marshalling deadlock (review C-3.5).
    auto reconcile_surfaces() -> void;

    /// @brief ensure the default pipeline and placeholder texture exist.
    /// lazy because pipeline creation needs at least one live swapchain.
    auto ensure_default_resources() -> void;

    /// @brief extract: walk the world once, resolve asset caches, build and
    /// sort the command list, snapshot per-window camera data by value.
    auto extract_frame() -> void;

    /// @brief replay the extracted frame for one window: begin frame, write
    /// this window's camera ubo, record command chunks on job workers,
    /// execute, end frame.
    auto render_window(const window_camera& wc) -> void;
};

}  // namespace vent
