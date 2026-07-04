#pragma once
//
// vent math library.
// 4x4 matrix.
// ——————————————————————
//
// a standard 4x4 floating-point matrix stored in column-major order.
// implements a right-handed z-up coordinate system for transformations.

#include <_vent/math/vec3.hpp>
#include <_vent/math/vec4.hpp>

#include <cmath>

namespace vent::math {

/// @brief a 4x4 floating-point matrix in column-major order.
struct mat4 {
    /// @brief column-major data [col][row].
    float data[4][4] = {{0}};

    /// @brief default constructor initializes to zero.
    constexpr mat4() = default;

    /// @brief identity matrix factory.
    static constexpr auto identity() -> mat4 {
        mat4 m;
        m.data[0][0] = 1.0f;
        m.data[1][1] = 1.0f;
        m.data[2][2] = 1.0f;
        m.data[3][3] = 1.0f;
        return m;
    }

    // --- operators ---

    constexpr auto operator*(const mat4& rhs) const -> mat4 {
        mat4 res;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                res.data[c][r] =
                    data[0][r] * rhs.data[c][0] + data[1][r] * rhs.data[c][1] +
                    data[2][r] * rhs.data[c][2] + data[3][r] * rhs.data[c][3];
            }
        }
        return res;
    }

    constexpr auto operator*(const vec4& v) const -> vec4 {
        return {data[0][0] * v.x + data[1][0] * v.y + data[2][0] * v.z +
                    data[3][0] * v.w,
                data[0][1] * v.x + data[1][1] * v.y + data[2][1] * v.z +
                    data[3][1] * v.w,
                data[0][2] * v.x + data[1][2] * v.y + data[2][2] * v.z +
                    data[3][2] * v.w,
                data[0][3] * v.x + data[1][3] * v.y + data[2][3] * v.z +
                    data[3][3] * v.w};
    }
};

// --- transformations ---

/// @brief creates a translation matrix.
constexpr auto translate(const vec3& v) -> mat4 {
    mat4 m       = mat4::identity();
    m.data[3][0] = v.x;
    m.data[3][1] = v.y;
    m.data[3][2] = v.z;
    return m;
}

/// @brief creates a scaling matrix.
constexpr auto scale(const vec3& v) -> mat4 {
    mat4 m       = mat4::identity();
    m.data[0][0] = v.x;
    m.data[1][1] = v.y;
    m.data[2][2] = v.z;
    return m;
}

/// @brief creates a rotation matrix around the z-axis (yaw).
inline auto rotate_z(float angle_radians) -> mat4 {
    mat4  m      = mat4::identity();
    float c      = std::cos(angle_radians);
    float s      = std::sin(angle_radians);
    m.data[0][0] = c;
    m.data[0][1] = s;
    m.data[1][0] = -s;
    m.data[1][1] = c;
    return m;
}

/// @brief creates a view matrix for a right-handed z-up coordinate system.
/// @param eye the position of the camera.
/// @param center the point the camera is looking at.
/// @param up the up vector (usually 0, 0, 1).
/// @return a view matrix that maps world space to eye space (x=right,
/// y=forward, z=up).
inline auto look_at(const vec3& eye, const vec3& center, const vec3& up)
    -> mat4 {
    vec3 const f = normalize(center - eye);  // forward (y)
    vec3 const s = normalize(cross(f, up));  // right (x)
    vec3 const u = cross(s, f);              // up (z)

    mat4 m       = mat4::identity();
    m.data[0][0] = s.x;
    m.data[1][0] = s.y;
    m.data[2][0] = s.z;

    m.data[0][1] = f.x;
    m.data[1][1] = f.y;
    m.data[2][1] = f.z;

    m.data[0][2] = u.x;
    m.data[1][2] = u.y;
    m.data[2][2] = u.z;

    m.data[3][0] = -dot(s, eye);
    m.data[3][1] = -dot(f, eye);
    m.data[3][2] = -dot(u, eye);

    return m;
}

/// @brief creates a perspective projection matrix mapped to vulkan's clip space.
/// vulkan clip space expects x=right, y=down, z=0..1.
/// this projection matrix takes a right-handed z-up eye space (x=right,
/// y=forward, z=up) and outputs vulkan clip space.
/// @param fovy_radians vertical field of view in radians.
/// @param aspect aspect ratio (width / height).
/// @param z_near near clipping plane distance.
/// @param z_far far clipping plane distance.
inline auto perspective(float fovy_radians,
                        float aspect,
                        float z_near,
                        float z_far) -> mat4 {
    mat4        m;
    float const cot = 1.0f / std::tan(fovy_radians / 2.0f);

    // map eye x (right) to clip x (right)
    m.data[0][0] = cot / aspect;

    // map eye z (up) to clip -y (down)
    m.data[2][1] = -cot;

    // map eye y (forward depth) to clip z (0..1)
    m.data[1][2] = z_far / (z_far - z_near);
    m.data[3][2] = -(z_far * z_near) / (z_far - z_near);

    // set w to eye y for perspective divide
    m.data[1][3] = 1.0f;

    return m;
}

}  // namespace vent::math
