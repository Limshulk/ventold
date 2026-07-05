#pragma once
//
// vent public sdk.
// uniform buffer structures.
// ——————————————————————
//
// defines the standard uniform buffer layout used by the engine.

#include <_vent/vent_sdk.hpp>

#include <_vent/math/math.hpp>

namespace vent {

/// @brief standard global uniform buffer object containing transformation matrices.
struct uniform_buffer_object {
    alignas(16) math::mat4 view;
    alignas(16) math::mat4 proj;
};

}  // namespace vent
