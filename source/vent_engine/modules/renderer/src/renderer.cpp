// renderer module.
// renderer frontend system implementation.
// ——————————————————————
//
// the render phase of the frame. on_update() runs after the client's simulate
// phase and performs: reconcile surfaces → extract the world once → render
// each window from the extracted snapshot. no client involvement anywhere.

#include <renderer.hpp>

#include <_vent/accessors.hpp>
#include <_vent/renderer/pipeline_desc.hpp>
#include <_vent/renderer/texture_desc.hpp>
#include <_vent/renderer/uniform_buffer.hpp>

#include <core/interfaces/i_system_registry.hpp>
#include <core/system/registration.hpp>

#include <platform/interfaces/i_window.hpp>

#include <algorithm>

namespace vent {

// --- lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

auto renderer_system::initialize() -> bool {

    // load the render backend using the system registry.
    // todo: for now, we only have one backend. once there are multiple, we
    // todo: should decide on a selection strategy. config, envar, runtime, ...

    log()->trace("renderer", "loading render backend plugin...");
    auto& registry = static_cast<i_system_registry&>(system());
    if (!registry.load_plugin_library("vent_vulkan_backend")) {
        log()->error("renderer", "failed to load vulkan backend plugin.");
        return false;
    }

    log()->trace("renderer", "initializing render backend plugin...");
    if (!registry.initialize_plugin_systems("vent_vulkan_backend")) {
        log()->error("renderer", "failed to initialize vulkan backend plugin.");
        return false;
    }

    _backend = system().get<i_render_backend>();

    log()->info("renderer",
                "render backend plugin loaded: {}",
                _backend->get_api_name());

    // note: no window event subscriptions here anymore. surfaces are
    // reconciled against the platform's window list at the start of every
    // frame (reconcile_surfaces) — deterministic, main-thread, and immune to
    // the worker-thread/platform-thread marshalling deadlock that event
    // callbacks risked (review c-3.5). windows created before the renderer
    // initialized are picked up by the first reconcile pass.

    return true;
}

auto renderer_system::shutdown() -> void {
    log()->trace("renderer", "renderer_system::shutdown() called.");

    if (!_backend) {
        return;
    }

    // one barrier at the boundary: a single wait-for-idle before teardown
    // beats scattered per-resource stalls.
    _backend->wait_for_idle();
    log()->trace(
        "renderer",
        "renderer_system::shutdown(): backend completed all operations.");

    {
        std::lock_guard lock(_model_mutex);
        for (auto& [path, cached_m] : _model_cache) {
            if (cached_m.mesh != INVALID_MESH_HANDLE) {
                _backend->destroy_mesh(cached_m.mesh);
            }
        }
        _model_cache.clear();
    }

    {
        std::lock_guard lock(_texture_mutex);
        for (auto& [path, cached_t] : _texture_cache) {
            if (cached_t.texture != INVALID_TEXTURE_HANDLE) {
                _backend->destroy_texture(cached_t.texture);
            }
        }
        _texture_cache.clear();
    }

    if (_placeholder_texture != INVALID_TEXTURE_HANDLE) {
        _backend->destroy_texture(_placeholder_texture);
        _placeholder_texture = INVALID_TEXTURE_HANDLE;
    }

    if (_default_pipeline != INVALID_PIPELINE_HANDLE) {
        _backend->destroy_graphics_pipeline(_default_pipeline);
        _default_pipeline = INVALID_PIPELINE_HANDLE;
    }

    // destroy all remaining surfaces (the backend defers actual destruction
    // safely; its own shutdown then drains whatever is left).
    for (auto* w : _backend->get_surface_windows()) {
        _backend->destroy_surface(w);
    }
}

// --- ir_runnable implementation ---
// —————————————————————————————————————————————————————————————————————————————

auto renderer_system::on_update(f64 delta_time) -> void {
    (void) delta_time;

    if (!_backend) {
        return;
    }

    reconcile_surfaces();
    ensure_default_resources();
    extract_frame();

    // per-window replay: everything below reads only the frame snapshot
    // (_command_list + _window_cameras), never live world state.
    for (const auto& wc : _window_cameras) {
        render_window(wc);
    }
}

// --- frame stages ---
// —————————————————————————————————————————————————————————————————————————————

auto renderer_system::reconcile_surfaces() -> void {
    // converge backend surfaces to the platform's window list once per frame.
    // creation: any window without a surface gets one (covers both startup
    // windows and windows created at runtime, like minimal's delayed window).
    auto windows = platform()->get_windows();
    for (auto* w : windows) {
        if (w && !_backend->has_surface(w)) {
            if (_backend->create_surface(w)) {
                log()->info("renderer",
                            "surface created for window '{}'.",
                            w->get_title());
            } else {
                log()->error("renderer",
                             "failed to create surface for window '{}'.",
                             w->get_title());
            }
        }
    }

    // destruction: surfaces whose window vanished from the platform list are
    // marked for deferred destruction. the window pointer is only COMPARED
    // here, never dereferenced — it may already be dead (platform-initiated
    // close). proper lifetime safety needs generation-checked window handles
    // (review c-5, phase 2); until then this is exactly as safe as the old
    // event-driven path, minus the worker-thread hazards.
    for (auto* sw : _backend->get_surface_windows()) {
        if (std::find(windows.begin(), windows.end(), sw) == windows.end()) {
            _backend->destroy_surface(sw);
        }
    }
}

auto renderer_system::ensure_default_resources() -> void {
    // 1. default pipeline. lazy because pipeline creation requires at least
    // one live swapchain for format information.
    if (_default_pipeline == INVALID_PIPELINE_HANDLE && !_pipeline_failed) {
        // engine-owned default shader from the vent:// mount — the renderer
        // must never reach into the app's asset space (review s-2).
        _default_shader =
            asset()->load_shader("vent://shaders/default.slang.spv");
        if (!_default_shader) {
            // classic magenta error shader as fallback: a wrong-but-visible
            // frame is debuggable, a black screen is not.
            log()->error("renderer",
                         "default shader missing — falling back to the error "
                         "shader.");
            _default_shader =
                asset()->load_shader("vent://shaders/error.slang.spv");
        }

        if (_default_shader) {
            pipeline_desc p_desc;
            p_desc.shader     = _default_shader;
            _default_pipeline = _next_pipeline_handle.fetch_add(1);
            _backend->create_graphics_pipeline(_default_pipeline, p_desc);
        } else {
            _pipeline_failed = true;
            log()->error("renderer",
                         "no usable shader found (default + error both "
                         "missing) — rendering disabled. will not retry.");
        }
    }

    // 2. placeholder texture: 64x64 magenta/black checkerboard, generated in
    // code — no file, no i/o, cannot be missing. used for entities whose
    // texture failed to load; phase 2 reuses it as the "still loading" state.
    if (_placeholder_texture == INVALID_TEXTURE_HANDLE) {
        constexpr u32   size  = 64;
        constexpr u32   block = 8;  // 8x8 pixel checker blocks.
        std::vector<u8> pixels(size * size * 4);
        for (u32 y = 0; y < size; ++y) {
            for (u32 x = 0; x < size; ++x) {
                const bool magenta   = ((x / block) + (y / block)) % 2 == 0;
                u8*        px        = &pixels[(y * size + x) * 4];
                px[0]                = magenta ? 255 : 0;  // r.
                px[1]                = 0;                  // g.
                px[2]                = magenta ? 255 : 0;  // b.
                px[3]                = 255;                // a.
            }
        }

        texture_desc t_desc {.width  = size,
                             .height = size,
                             .pixels = std::span<const u8>(pixels)};
        _placeholder_texture = _next_texture_handle.fetch_add(1);
        _backend->create_texture(_placeholder_texture, t_desc);
    }
}

auto renderer_system::extract_frame() -> void {
    // --- scene extraction: once per frame, not per window (perf §7.4) ---

    _command_list.clear();

    if (world() && _default_pipeline != INVALID_PIPELINE_HANDLE) {
        auto entities = world()->get_renderable_entities();

        for (entity e : entities) {
            const auto* mesh_comp  = world()->get_mesh(e);
            const auto* trans_comp = world()->get_transform(e);

            if (!mesh_comp || mesh_comp->model_path.empty()) {
                continue;
            }

            // resolve model.
            // note: these caches are owned by the render phase (extract runs
            // on the main thread; shutdown runs after the loop stops) so
            // there is no real contention, but we lock consistently with
            // shutdown()'s use of the same mutexes rather than guarding only
            // one side. the `failed` flag stops a broken asset from being
            // re-loaded from disk (and re-logged) every single frame.
            mesh_handle m_handle = INVALID_MESH_HANDLE;
            {
                std::lock_guard lock(_model_mutex);
                auto&           cached_m = _model_cache[mesh_comp->model_path];
                if (!cached_m.failed && cached_m.mesh == INVALID_MESH_HANDLE) {
                    cached_m.asset = asset()->load_model(mesh_comp->model_path);
                    if (cached_m.asset && !cached_m.asset->vertices.empty()) {
                        cached_m.mesh = _next_mesh_handle.fetch_add(1);
                        _backend->create_mesh(cached_m.mesh,
                                              cached_m.asset->vertices,
                                              cached_m.asset->indices);
                    } else {
                        cached_m.failed = true;
                        log()->error(
                            "renderer",
                            "failed to load model '{}'; will not retry.",
                            mesh_comp->model_path);
                    }
                }
                m_handle = cached_m.mesh;
            }

            // resolve texture (same ownership / failed-flag reasoning). a
            // missing or failed texture falls back to the checkerboard
            // placeholder so the entity stays visible — failure as a visual
            // state instead of an absence.
            texture_handle t_handle = _placeholder_texture;
            if (!mesh_comp->texture_path.empty()) {
                std::lock_guard lock(_texture_mutex);
                auto& cached_t = _texture_cache[mesh_comp->texture_path];
                if (!cached_t.failed &&
                    cached_t.texture == INVALID_TEXTURE_HANDLE) {
                    cached_t.asset =
                        asset()->load_image(mesh_comp->texture_path);
                    if (cached_t.asset) {
                        texture_desc t_desc {
                            .width  = cached_t.asset->width,
                            .height = cached_t.asset->height,
                            .pixels =
                                std::span<const u8>(cached_t.asset->pixels)};
                        cached_t.texture = _next_texture_handle.fetch_add(1);
                        _backend->create_texture(cached_t.texture, t_desc);
                    } else {
                        cached_t.failed = true;
                        log()->error(
                            "renderer",
                            "failed to load texture '{}'; using placeholder. "
                            "will not retry.",
                            mesh_comp->texture_path);
                    }
                }
                if (cached_t.texture != INVALID_TEXTURE_HANDLE) {
                    t_handle = cached_t.texture;
                }
            }

            if (m_handle != INVALID_MESH_HANDLE &&
                t_handle != INVALID_TEXTURE_HANDLE) {
                math::mat4 transform =
                    trans_comp ? trans_comp->matrix : math::mat4::identity();
                // using entity id as sort key for now. real key packing
                // (layer|pipeline|texture|depth) is roadmap phase 3.1 — the
                // bind-on-change elision in the backend already exploits
                // whatever adjacency this sort produces.
                _command_list.draw_mesh(
                    _default_pipeline, t_handle, m_handle, transform, e);
            }
        }

        _command_list.sort();
    }

    // --- camera snapshot: resolve each window's camera BY VALUE ---
    // after this point the render stage never reads live world state. this
    // is the extract/render seam that later lets simulation overlap rendering.

    _window_cameras.clear();
    for (auto* w : platform()->get_windows()) {
        if (!w || !_backend->has_surface(w)) {
            continue;
        }

        window_camera wc;
        wc.window = w;

        entity cam_entity =
            world() ? world()->get_active_camera(w) : INVALID_ENTITY;
        const camera_component* cam =
            (cam_entity != INVALID_ENTITY) ? world()->get_camera(cam_entity)
                                           : nullptr;

        if (cam) {
            wc.camera = *cam;
            if (const auto* t = world()->get_transform(cam_entity)) {
                wc.pose = t->matrix;
            }
        } else {
            // built-in fallback camera: every window renders SOMETHING with
            // zero configuration. warn once, not per frame.
            wc.pose = math::look_at_transform(math::vec3(3.0f, 3.0f, 3.0f),
                                              math::vec3(0.0f, 0.0f, 0.0f),
                                              math::vec3(0.0f, 0.0f, 1.0f));
            if (!_warned_no_camera) {
                _warned_no_camera = true;
                log()->warn("renderer",
                            "no camera entity in the world — using the "
                            "built-in fallback camera. create an entity with "
                            "a camera_component to control the view.");
            }
        }

        _window_cameras.push_back(wc);
    }
}

auto renderer_system::render_window(const window_camera& wc) -> void {
    // begin_frame waits on THIS window's frame fence — everything written
    // between here and end_frame is protected by exactly that fence.
    if (!_backend->begin_frame(wc.window)) {
        return;  // minimized, out of date, or surface just destroyed.
    }

    // view = inverse of the camera pose (rigid transforms invert cheaply).
    // projection from the window's ACTUAL framebuffer size, so resizing
    // fixes the aspect live. 0-height guard: minimized windows are rejected
    // by begin_frame already, but a live 0-height resize must not produce
    // NaNs.
    const u32 fb_w   = wc.window->get_framebuffer_width();
    const u32 fb_h   = wc.window->get_framebuffer_height();
    f32       aspect = 16.0f / 9.0f;
    if (fb_h > 0) {
        aspect = static_cast<f32>(fb_w) / static_cast<f32>(fb_h);
    }

    uniform_buffer_object ubo = {
        .view = math::inverse_rigid(wc.pose),
        .proj = math::perspective(math::radians(wc.camera.fov_y_deg),
                                  aspect,
                                  wc.camera.z_near,
                                  wc.camera.z_far)};
    _backend->update_frame_uniforms(ubo);

    // record the extracted packets in parallel chunks on job workers.
    auto packets = _command_list.get_packets();
    if (!packets.empty()) {
        const usize chunk_size = 256;
        const usize num_chunks = (packets.size() + chunk_size - 1) / chunk_size;

        std::vector<vent::task> tasks;
        tasks.reserve(num_chunks);

        for (usize i = 0; i < num_chunks; ++i) {
            usize start = i * chunk_size;
            usize end   = (std::min) (start + chunk_size, packets.size());
            auto  chunk = std::span<const render_packet>(packets.data() + start,
                                                        end - start);

            tasks.push_back(job()->submit([this, chunk]() -> void* {
                return _backend->record_command_chunk(chunk);
            }));
        }

        std::vector<void*> secondary_cmds;
        secondary_cmds.reserve(num_chunks);
        for (auto& task : tasks) {
            secondary_cmds.push_back(task.get<void*>());
        }

        _backend->execute_recorded_commands(secondary_cmds);
    }

    _backend->end_frame(wc.window);
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::renderer_system,
                     vent::i_renderer,
                     vent::ic_renderer);
