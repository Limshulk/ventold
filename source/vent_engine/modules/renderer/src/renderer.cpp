// renderer module.
// renderer frontend system implementation.
// ——————————————————————
//
// details.

#include <renderer.hpp>

#include <_vent/accessors.hpp>

#include <core/interfaces/i_system_registry.hpp>
#include <core/system/registration.hpp>

#include <renderer/interfaces/i_pipeline.hpp>
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

auto renderer_system::end_frame(ic_window* window) -> void {
    if (_backend) {
        _backend->end_frame(window);
    }
}

auto renderer_system::create_graphics_pipeline(const pipeline_desc& desc)
    -> std::unique_ptr<ic_pipeline> {
    log()->trace("renderer", "creating graphics pipeline");
    if (_backend) {
        return _backend->create_graphics_pipeline(desc);
    }
    return nullptr;
}

auto renderer_system::bind_pipeline(ic_pipeline* pipeline) -> void {
    if (_backend) {
        _backend->bind_pipeline(pipeline);
    }
}

auto renderer_system::create_mesh(std::span<const vertex>   vertices,
                                  std::span<const uint32_t> indices)
    -> mesh_handle {
    log()->trace("renderer", "creating mesh ({} vertices, {} indices)", vertices.size(), indices.size());
    if (!_backend)
        return INVALID_MESH_HANDLE;
    return _backend->create_mesh(vertices, indices);
}

auto renderer_system::get_command_list() -> command_list& {
    thread_local command_list tls_cmd_list;
    return tls_cmd_list;
}

auto renderer_system::submit_command_lists(std::span<command_list* const> lists) -> void {
    if (!_backend) return;

    usize total_packets = 0;
    for (auto* list : lists) {
        list->sort();
        usize count = list->get_packets().size();
        total_packets += count;
    }

    std::vector<render_packet> all_packets;
    all_packets.reserve(total_packets);
    for (auto* list : lists) {
        auto packets = list->get_packets();
        all_packets.insert(all_packets.end(), packets.begin(), packets.end());
    }

    std::ranges::sort(all_packets, [](const render_packet& a, const render_packet& b) {
        return a.key < b.key;
    });

    _backend->execute_packets(all_packets);

    for (auto* list : lists) {
        list->clear();
    }
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::renderer_system,
                     vent::i_renderer,
                     vent::ic_renderer);