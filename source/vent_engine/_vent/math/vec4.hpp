#pragma once
//
// vent math library.
// 4d vector.
// ——————————————————————
//
// a standard 4-component floating-point vector.

#include <cmath>

namespace vent::math {

/// @brief a 4-component floating-point vector.
struct vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    /// @brief default constructor.
    constexpr vec4() = default;

    /// @brief constructor with individual components.
    constexpr vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    /// @brief constructor with a single scalar for all components.
    explicit constexpr vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}

    // --- operators ---

    constexpr auto operator+(const vec4& rhs) const -> vec4 {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }

    constexpr auto operator-(const vec4& rhs) const -> vec4 {
        return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }

    constexpr auto operator*(const vec4& rhs) const -> vec4 {
        return {x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w};
    }

    constexpr auto operator*(float scalar) const -> vec4 {
        return {x * scalar, y * scalar, z * scalar, w * scalar};
    }

    constexpr auto operator/(float scalar) const -> vec4 {
        return {x / scalar, y / scalar, z / scalar, w / scalar};
    }

    constexpr auto operator+=(const vec4& rhs) -> vec4& {
        x += rhs.x; y += rhs.y; z += rhs.z; w += rhs.w; return *this;
    }

    constexpr auto operator-=(const vec4& rhs) -> vec4& {
        x -= rhs.x; y -= rhs.y; z -= rhs.z; w -= rhs.w; return *this;
    }

    constexpr auto operator*=(float scalar) -> vec4& {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this;
    }

    constexpr auto operator/=(float scalar) -> vec4& {
        x /= scalar; y /= scalar; z /= scalar; w /= scalar; return *this;
    }
};

/// @brief scalar multiplication with vector on the right.
constexpr auto operator*(float scalar, const vec4& vec) -> vec4 {
    return vec * scalar;
}

// --- functions ---

/// @brief computes the dot product of two vectors.
constexpr auto dot(const vec4& a, const vec4& b) -> float {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// @brief computes the squared length of the vector.
constexpr auto length_sq(const vec4& v) -> float {
    return dot(v, v);
}

/// @brief computes the length of the vector.
inline auto length(const vec4& v) -> float {
    return std::sqrt(length_sq(v));
}

/// @brief returns a normalized copy of the vector.
inline auto normalize(const vec4& v) -> vec4 {
    float len = length(v);
    if (len > 0.0f) {
        return v / len;
    }
    return v;
}

} // namespace vent::math
