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

// forward declarations.
class ic_window;

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

/// @brief makes an entity a camera. the camera is scene state, not a render
/// setting: the renderer derives the view matrix from the entity's transform
/// (the camera pose, i.e. camera-to-world) and the projection from these
/// parameters plus the target window's actual framebuffer aspect each frame —
/// so resizing a window fixes the aspect automatically and clients never
/// touch a matrix.
struct camera_component {
    f32 fov_y_deg = 60.0f;   ///< vertical field of view in degrees.
    f32 z_near    = 0.1f;    ///< near clip plane distance.
    f32 z_far     = 100.0f;  ///< far clip plane distance.
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

    /// @brief attach or update a camera component on an entity.
    virtual auto set_camera(entity e, const camera_component& camera)
        -> void = 0;

    /// @brief retrieve the camera component of an entity.
    /// @return pointer to the component, or nullptr if it doesn't exist.
    virtual auto get_camera(entity e) const -> const camera_component* = 0;

    // --- camera assignment ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief assign a camera entity to a window (or set the default camera).
    /// @param e the camera entity (must have a camera component to render).
    /// @param window the window this camera renders to. nullptr sets the
    /// default camera used by every window without an explicit assignment.
    virtual auto set_active_camera(entity e, ic_window* window = nullptr)
        -> void = 0;

    /// @brief resolve the camera entity for a window. resolution order:
    /// explicit assignment → default camera → first entity with a camera
    /// component. the renderer falls back to a built-in camera if this
    /// returns INVALID_ENTITY, so every window always renders something.
    /// @return the camera entity, or INVALID_ENTITY if none exists.
    [[nodiscard]]
    virtual auto get_active_camera(const ic_window* window) const
        -> entity = 0;

    // --- system queries ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief get a list of all active entities that have a mesh component.
    /// this is primarily used by the renderer to draw the scene.
    virtual auto get_renderable_entities() const -> std::span<const entity> = 0;
};

}  // namespace vent
