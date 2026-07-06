//
// world module.
// world system implementation.
// ——————————————————————

#include <world_system.hpp>

#include <_vent/accessors.hpp>
#include <core/system/registration.hpp>

#include <algorithm>

namespace vent {

auto world_system::on_initialization() -> system_initialization_status {
    log()->trace("world", "initializing world system...");
    return system_initialization_status::success;
}

auto world_system::on_shutdown() -> void {
    log()->trace("world", "shutting down world system...");
    _active_entities.clear();
    _renderable_entities.clear();
    _camera_entities.clear();
    _transforms.clear();
    _meshes.clear();
    _cameras.clear();
    _window_cameras.clear();
    _default_camera = INVALID_ENTITY;
}

auto world_system::create_entity() -> entity {
    entity e = _next_entity_id++;
    _active_entities.push_back(e);
    return e;
}

auto world_system::destroy_entity(entity e) -> void {
    std::erase(_active_entities, e);
    std::erase(_renderable_entities, e);
    std::erase(_camera_entities, e);
    _transforms.erase(e);
    _meshes.erase(e);
    _cameras.erase(e);

    // a destroyed camera must not stay assigned anywhere — clearing here
    // means get_active_camera falls back to the next resolution step instead
    // of returning a dead entity.
    if (_default_camera == e) {
        _default_camera = INVALID_ENTITY;
    }
    for (auto it = _window_cameras.begin(); it != _window_cameras.end();) {
        if (it->second == e) {
            it = _window_cameras.erase(it);
        } else {
            ++it;
        }
    }
}

auto world_system::set_transform(entity e, const transform_component& transform) -> void {
    _transforms[e] = transform;
}

auto world_system::get_transform(entity e) const -> const transform_component* {
    auto it = _transforms.find(e);
    if (it != _transforms.end()) {
        return &it->second;
    }
    return nullptr;
}

auto world_system::set_mesh(entity e, const mesh_component& mesh) -> void {
    bool was_renderable = _meshes.contains(e);
    _meshes[e] = mesh;
    
    if (!was_renderable) {
        _renderable_entities.push_back(e);
    }
}

auto world_system::get_mesh(entity e) const -> const mesh_component* {
    auto it = _meshes.find(e);
    if (it != _meshes.end()) {
        return &it->second;
    }
    return nullptr;
}

auto world_system::set_camera(entity e, const camera_component& camera) -> void {
    bool was_camera = _cameras.contains(e);
    _cameras[e] = camera;

    if (!was_camera) {
        _camera_entities.push_back(e);
    }
}

auto world_system::get_camera(entity e) const -> const camera_component* {
    auto it = _cameras.find(e);
    if (it != _cameras.end()) {
        return &it->second;
    }
    return nullptr;
}

auto world_system::set_active_camera(entity e, ic_window* window) -> void {
    if (window) {
        _window_cameras[window] = e;
    } else {
        _default_camera = e;
    }
}

auto world_system::get_active_camera(const ic_window* window) const -> entity {
    // resolution order (documented in ic_world.hpp): explicit per-window
    // assignment → default camera → first entity that has a camera component.
    // every step re-validates that the entity still owns a camera component,
    // so a stale assignment degrades gracefully instead of rendering garbage.
    if (window) {
        auto it = _window_cameras.find(window);
        if (it != _window_cameras.end() && _cameras.contains(it->second)) {
            return it->second;
        }
    }

    if (_default_camera != INVALID_ENTITY &&
        _cameras.contains(_default_camera)) {
        return _default_camera;
    }

    if (!_camera_entities.empty()) {
        return _camera_entities.front();
    }

    return INVALID_ENTITY;
}

auto world_system::get_renderable_entities() const -> std::span<const entity> {
    return _renderable_entities;
}

}  // namespace vent

// system registration.
VENT_REGISTER_SYSTEM(vent::world_system, vent::i_world, vent::ic_world);
