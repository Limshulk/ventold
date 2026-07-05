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
    _transforms.clear();
    _meshes.clear();
}

auto world_system::create_entity() -> entity {
    entity e = _next_entity_id++;
    _active_entities.push_back(e);
    return e;
}

auto world_system::destroy_entity(entity e) -> void {
    std::erase(_active_entities, e);
    std::erase(_renderable_entities, e);
    _transforms.erase(e);
    _meshes.erase(e);
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

auto world_system::get_renderable_entities() const -> std::span<const entity> {
    return _renderable_entities;
}

}  // namespace vent

// system registration.
VENT_REGISTER_SYSTEM(vent::world_system, vent::i_world, vent::ic_world);
