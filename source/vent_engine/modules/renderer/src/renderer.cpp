// renderer module.
// renderer frontend system implementation.
// ——————————————————————
//
// details.

#include <renderer.hpp>

#include <_vent/accessors.hpp>
#include <_vent/renderer/uniform_buffer.hpp>

#include <core/interfaces/i_system_registry.hpp>
#include <core/system/registration.hpp>

#include <platform/interfaces/i_window.hpp>

namespace vent {

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

    log()->trace("renderer",
                 "render backend initialized. "
                 "subscribing to window.created & window.destroyed events...");

    // globally subscribe to all window creations so we can attach a surface to
    // each.
    _window_sub = event()->subscribe(
        "window.created", [this](std::string_view, void* data) {
            auto* w = static_cast<ic_window*>(data);
            if (w) {
                log()->trace("renderer",
                             "window.created event received for '{}'",
                             w->get_title());
                std::lock_guard lock(_windows_mutex);
                _windows.push_back(w);
                if (_backend) {
                    if (_backend->create_surface(w)) {
                        log()->info(
                            "renderer",
                            "surface dynamically created for window '{}'.",
                            w->get_title());
                    } else {
                        log()->error(
                            "renderer",
                            "failed to create surface for window '{}'.",
                            w->get_title());
                    }
                }
            }
        });

    _window_destroyed_sub = event()->subscribe(
        "window.destroyed", [this](std::string_view, void* data) {
            auto* w = static_cast<ic_window*>(data);
            if (w) {
                log()->trace("renderer",
                             "window.destroyed event received for '{}'",
                             w->get_title());
                std::lock_guard lock(_windows_mutex);
                auto it = std::find(_windows.begin(), _windows.end(), w);
                if (it != _windows.end()) {
                    if (_backend) {
                        _backend->destroy_surface(w);
                    }
                    _windows.erase(it);
                }
            }
        });

    // check if platform already has active windows, and initialize surfaces for
    // them immediately.
    log()->trace("renderer", "checking for existing windows...");
    if (auto* plat = platform_if_ready()) {
        auto existing_windows = plat->get_windows();
        if (!existing_windows.empty()) {
            log()->info(
                "renderer",
                "found {} existing windows. creating surfaces for them.",
                existing_windows.size());
            std::lock_guard lock(_windows_mutex);
            for (auto* w : existing_windows) {
                // only add if not already present.
                if (std::find(_windows.begin(), _windows.end(), w) ==
                    _windows.end()) {
                    _windows.push_back(w);
                    if (_backend) {
                        if (_backend->create_surface(w)) {
                            log()->info("renderer",
                                        "surface dynamically created for "
                                        "existing window '{}'.",
                                        w->get_title());
                        } else {
                            log()->error("renderer",
                                         "failed to create surface for "
                                         "existing window '{}'.",
                                         w->get_title());
                        }
                    }
                }
            }
        } else {
            log()->trace("renderer", "no existing windows found.");
        }
    }

    return true;
}

auto renderer_system::shutdown() -> void {
    log()->trace("renderer", "renderer_system::shutdown() called");

    if (_window_sub != vent::INVALID_SUBSCRIPTION && event()) {
        event()->unsubscribe(_window_sub);
        _window_sub = vent::INVALID_SUBSCRIPTION;
    }
    if (_window_destroyed_sub != vent::INVALID_SUBSCRIPTION && event()) {
        event()->unsubscribe(_window_destroyed_sub);
        _window_destroyed_sub = vent::INVALID_SUBSCRIPTION;
    }

    // destroy surfaces before backend is shut down.
    if (_backend) {
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

        if (_default_pipeline != INVALID_PIPELINE_HANDLE) {
            _backend->destroy_graphics_pipeline(_default_pipeline);
            _default_pipeline = INVALID_PIPELINE_HANDLE;
        }

        std::lock_guard lock(_windows_mutex);
        for (auto* w : _windows) {
            _backend->destroy_surface(static_cast<ic_window*>(w));
        }
        _windows.clear();
    }
}

// --- ic_renderer implementation ---
// —————————————————————————————————————————————————————————————————————————————

auto renderer_system::set_frames_in_flight(ic_window* window, u32 frames)
    -> void {
    log()->trace("renderer", "setting frames in flight to {}", frames);
    if (_backend) {
        _backend->set_frames_in_flight(window, frames);
    }
}

auto renderer_system::begin_frame(ic_window* window) -> bool {
    if (_backend) {
        return _backend->begin_frame(window);
    }
    return false;
}

auto renderer_system::set_camera(const math::mat4& view, const math::mat4& proj)
    -> void {
    _view_matrix = view;
    _proj_matrix = proj;
}

auto renderer_system::end_frame(ic_window* window) -> void {
    if (!_backend || !world()) {
        if (_backend)
            _backend->end_frame(window);
        return;
    }

    // 1. initialize default pipeline if not done yet
    if (_default_pipeline == INVALID_PIPELINE_HANDLE) {
        _default_shader =
            asset()->load_shader("app://assets/shaders/shader.slang.spv");
        if (_default_shader) {
            pipeline_desc p_desc;
            p_desc.shader     = _default_shader;
            _default_pipeline = _next_pipeline_handle.fetch_add(1);
            _backend->create_graphics_pipeline(_default_pipeline, p_desc);
        }
    }

    // 2. update global uniforms (camera)
    uniform_buffer_object ubo = {.view = _view_matrix, .proj = _proj_matrix};
    _backend->update_global_uniforms(ubo);

    // 3. fetch entities and build command list
    _command_list.clear();
    auto entities = world()->get_renderable_entities();

    for (entity e : entities) {
        const auto* mesh_comp  = world()->get_mesh(e);
        const auto* trans_comp = world()->get_transform(e);

        if (!mesh_comp || mesh_comp->model_path.empty() ||
            mesh_comp->texture_path.empty())
            continue;

        // resolve model
        mesh_handle m_handle = INVALID_MESH_HANDLE;
        auto&       cached_m = _model_cache[mesh_comp->model_path];
        if (cached_m.mesh == INVALID_MESH_HANDLE) {
            cached_m.asset = asset()->load_model(mesh_comp->model_path);
            if (cached_m.asset && !cached_m.asset->vertices.empty()) {
                cached_m.mesh = _next_mesh_handle.fetch_add(1);
                _backend->create_mesh(cached_m.mesh,
                                      cached_m.asset->vertices,
                                      cached_m.asset->indices);
            }
        }
        m_handle = cached_m.mesh;

        // resolve texture
        texture_handle t_handle = INVALID_TEXTURE_HANDLE;
        auto&          cached_t = _texture_cache[mesh_comp->texture_path];
        if (cached_t.texture == INVALID_TEXTURE_HANDLE) {
            cached_t.asset = asset()->load_image(mesh_comp->texture_path);
            if (cached_t.asset) {
                texture_desc t_desc {
                    .width  = cached_t.asset->width,
                    .height = cached_t.asset->height,
                    .pixels = std::span<const u8>(cached_t.asset->pixels)};
                cached_t.texture = _next_texture_handle.fetch_add(1);
                _backend->create_texture(cached_t.texture, t_desc);
            }
        }
        t_handle = cached_t.texture;

        if (m_handle != INVALID_MESH_HANDLE &&
            t_handle != INVALID_TEXTURE_HANDLE &&
            _default_pipeline != INVALID_PIPELINE_HANDLE) {
            math::mat4 transform =
                trans_comp ? trans_comp->matrix : math::mat4::identity();
            // using entity id as sort key for now
            _command_list.draw_mesh(
                _default_pipeline, t_handle, m_handle, transform, e);
        }
    }

    // 4. sort and chunk commands
    _command_list.sort();
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

    // 5. present frame
    _backend->end_frame(window);
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::renderer_system,
                     vent::i_renderer,
                     vent::ic_renderer);