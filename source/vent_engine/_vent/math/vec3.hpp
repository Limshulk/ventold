#pragma once
//
// vent math library.
// 3d vector.
// ——————————————————————
//
// a standard 3-component floating-point vector.

#include <cmath>

namespace vent::math {

/// @brief a 3-component floating-point vector.
struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    /// @brief default constructor.
    constexpr vec3() = default;

    /// @brief constructor with individual components.
    constexpr vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    /// @brief constructor with a single scalar for all components.
    explicit constexpr vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}

    // --- operators ---

    constexpr auto operator+(const vec3& rhs) const -> vec3 {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }

    constexpr auto operator-(const vec3& rhs) const -> vec3 {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    constexpr auto operator*(const vec3& rhs) const -> vec3 {
        return {x * rhs.x, y * rhs.y, z * rhs.z};
    }

    constexpr auto operator*(float scalar) const -> vec3 {
        return {x * scalar, y * scalar, z * scalar};
    }

    constexpr auto operator/(float scalar) const -> vec3 {
        return {x / scalar, y / scalar, z / scalar};
    }

    constexpr auto operator+=(const vec3& rhs) -> vec3& {
        x += rhs.x; y += rhs.y; z += rhs.z; return *this;
    }

    constexpr auto operator-=(const vec3& rhs) -> vec3& {
        x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this;
    }

    constexpr auto operator*=(float scalar) -> vec3& {
        x *= scalar; y *= scalar; z *= scalar; return *this;
    }

    constexpr auto operator/=(float scalar) -> vec3& {
        x /= scalar; y /= scalar; z /= scalar; return *this;
    }
};

/// @brief scalar multiplication with vector on the right.
constexpr auto operator*(float scalar, const vec3& vec) -> vec3 {
    return vec * scalar;
}

// --- functions ---

/// @brief computes the dot product of two vectors.
constexpr auto dot(const vec3& a, const vec3& b) -> float {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// @brief computes the cross product of two vectors.
constexpr auto cross(const vec3& a, const vec3& b) -> vec3 {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

/// @brief computes the squared length of the vector.
constexpr auto length_sq(const vec3& v) -> float {
    return dot(v, v);
}

/// @brief computes the length of the vector.
inline auto length(const vec3& v) -> float {
    return std::sqrt(length_sq(v));
}

/// @brief returns a normalized copy of the vector.
inline auto normalize(const vec3& v) -> vec3 {
    float len = length(v);
    if (len > 0.0f) {
        return v / len;
    }
    return v;
}

} // namespace vent::math
