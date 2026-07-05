#pragma once
//
// vent public sdk.
// vertex structures.
// ——————————————————————
//
// defines the standard vertex layout used by the engine.

#include <_vent/vent_sdk.hpp>

#include <_vent/math/math.hpp>

namespace vent {

/// @brief standard vertex structure for 3d meshes.
struct vertex {
    math::vec3 position;
    math::vec3 color;
};

}  // namespace vent
