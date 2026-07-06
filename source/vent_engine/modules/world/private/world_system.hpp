#pragma once
//
// world module.
// world system.
// ——————————————————————

#include <world/interfaces/i_world.hpp>

#include <_vent/core/ir_dependencies.hpp>
#include <_vent/system/system_base.hpp>
#include <_vent/log/ic_log.hpp>

#include <unordered_map>
#include <vector>

namespace vent {

class world_system final
    : public system_base
    , public i_world
    , public ir_dependencies {
public:
    world_system()           = default;
    ~world_system() override = default;

    VENT_NO_COPY_MOVE(world_system);

public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    
    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_world::system_name;
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override;
    
    auto on_shutdown() -> void override;

public:
    // --- ir_dependencies implementation ---
    // —————————————————————————————————————————————————————————————————————————
    
    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {ic_log::system_name};
        return deps;
    }

public:
    // --- ic_world implementation ---
    // —————————————————————————————————————————————————————————————————————————

    auto create_entity() -> entity override;
    auto destroy_entity(entity e) -> void override;

    auto set_transform(entity e, const transform_component& transform) -> void override;
    auto get_transform(entity e) const -> const transform_component* override;

    auto set_mesh(entity e, const mesh_component& mesh) -> void override;
    auto get_mesh(entity e) const -> const mesh_component* override;

    auto set_camera(entity e, const camera_component& camera) -> void override;
    auto get_camera(entity e) const -> const camera_component* override;

    auto set_active_camera(entity e, ic_window* window) -> void override;
    auto get_active_camera(const ic_window* window) const -> entity override;

    auto get_renderable_entities() const -> std::span<const entity> override;

private:
    entity _next_entity_id = 1;

    std::vector<entity> _active_entities;
    std::vector<entity> _renderable_entities; // cache of entities with a mesh component
    std::vector<entity> _camera_entities;  ///< cache of entities with a camera
                                           ///< component, in creation order —
                                           ///< "first camera" resolution must
                                           ///< be deterministic, and map
                                           ///< iteration order is not.

    std::unordered_map<entity, transform_component> _transforms;
    std::unordered_map<entity, mesh_component> _meshes;
    std::unordered_map<entity, camera_component> _cameras;

    /// @brief default camera used by windows without an explicit assignment.
    entity _default_camera = INVALID_ENTITY;

    /// @brief explicit per-window camera assignments. keyed by window pointer
    /// only for identity (never dereferenced here), so a destroyed window
    /// simply leaves a stale entry that resolves like any unassigned window.
    std::unordered_map<const ic_window*, entity> _window_cameras;
};

}  // namespace vent
