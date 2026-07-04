#pragma once
//
// vent math library.
// master header.
// ——————————————————————
//
// includes all math primitives and provides helper functions.

#include <_vent/math/vec3.hpp>
#include <_vent/math/vec4.hpp>
#include <_vent/math/mat4.hpp>

namespace vent::math {

constexpr float pi = 3.14159265358979323846f;

/// @brief converts degrees to radians.
constexpr auto radians(float degrees) -> float {
    return degrees * (pi / 180.0f);
}

/// @brief converts radians to degrees.
constexpr auto degrees(float radians) -> float {
    return radians * (180.0f / pi);
}

} // namespace vent::math
