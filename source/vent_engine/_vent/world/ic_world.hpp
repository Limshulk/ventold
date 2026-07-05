#pragma once
//
// vent public sdk.
// world module interface.
// ——————————————————————
//
// abstract interface for the global world system.
// provides an ecs-lite approach to entity and component management.

#include <_vent/vent_sdk.hpp>
#include <_vent/math/math.hpp>

#include <span>
#include <string>
#include <string_view>

namespace vent {

/// @brief an opaque handle representing an entity in the world.
using entity                    = u64;
constexpr entity INVALID_ENTITY = 0;

/// @brief a component that defines an entity's position, rotation, and scale.
struct transform_component {
    math::mat4 matrix = math::mat4::identity();
};

/// @brief a component that defines a visual mesh and material/texture for an
/// entity.
struct mesh_component {
    std::string model_path;
    std::string texture_path;
};

class ic_world {
public:
    virtual ~ic_world() = default;

    static constexpr std::string_view system_name = "vent.system.world";

    // --- entity management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create a new empty entity in the world.
    /// @return a unique entity handle.
    virtual auto create_entity() -> entity = 0;

    /// @brief destroy an entity and all its attached components.
    /// @param e the entity to destroy.
    virtual auto destroy_entity(entity e) -> void = 0;

    // --- component management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief attach or update a transform component on an entity.
    virtual auto set_transform(entity e, const transform_component& transform)
        -> void = 0;

    /// @brief retrieve the transform component of an entity.
    /// @return pointer to the component, or nullptr if it doesn't exist.
    virtual auto get_transform(entity e) const
        -> const transform_component* = 0;

    /// @brief attach or update a mesh component on an entity.
    virtual auto set_mesh(entity e, const mesh_component& mesh) -> void = 0;

    /// @brief retrieve the mesh component of an entity.
    /// @return pointer to the component, or nullptr if it doesn't exist.
    virtual auto get_mesh(entity e) const -> const mesh_component* = 0;

    // --- system queries ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a list of all active entities that have a mesh component.
    /// this is primarily used by the renderer to draw the scene.
    virtual auto get_renderable_entities() const -> std::span<const entity> = 0;
};

}  // namespace vent
