#pragma once
//
// renderer module.
// vertex structures.
// ——————————————————————
//
// defines the standard vertex layout used by the engine.

#include <_vent/vent_sdk.hpp>

#include <_vent/math/vector.hpp>

namespace vent {

/// @brief standard vertex structure for 3d meshes.
struct vertex {
    math::float3 position;
    math::float3 color;
};

// todo: this has no right to be here.
/// @brief opaque handle to a mesh loaded into the renderer.
using mesh_handle = u64;

constexpr mesh_handle INVALID_MESH_HANDLE = 0;

}  // namespace vent
