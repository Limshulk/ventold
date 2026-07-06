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
///
/// winding note (read before touching perspective() or the pipeline cull mode):
/// vent uses a right-handed, z-up world/eye basis, but perspective() negates the
/// y row to match vulkan's clip space (where +y points down). a determinant with
/// one negated axis flips handedness, which flips the on-screen winding of every
/// triangle. so geometry authored counter-clockwise in world space rasterizes
/// clockwise. the vulkan pipeline is configured accordingly (frontFace =
/// eClockwise, cullMode = eBack in vulkan_pipeline.cpp). change one, change both.
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

// a plain 4x4 float grid with no padding. the renderer memcpy's this into
// uniform buffers and push constants, so the exact 64-byte size is contractual.
static_assert(sizeof(mat4) == 64, "mat4 must be a tightly packed 16 floats.");

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

/// @brief inverts a rigid transform (rotation + translation only).
///
/// a camera's view matrix is the inverse of its world transform ("move the
/// world opposite to the camera"). a general 4x4 inverse is ~60 lines of
/// error-prone cofactor math — and unnecessary here: for a rigid transform
/// M = T * R the inverse is simply Rᵀ composed with -Rᵀ·t, because a rotation
/// matrix's transpose is its inverse.
///
/// precondition: the matrix must be rigid — orthonormal 3x3 rotation block,
/// no scale or shear. feeding a scaled matrix in produces a wrong result
/// silently, which is why this is named inverse_RIGID and not inverse.
/// @param m the rigid transform to invert (e.g. a camera pose).
/// @return the inverse transform (e.g. the view matrix).
constexpr auto inverse_rigid(const mat4& m) -> mat4 {
    mat4 r = mat4::identity();

    // transpose the 3x3 rotation block. data is [col][row], so the transpose
    // swaps column and row indices.
    for (int c = 0; c < 3; ++c) {
        for (int rw = 0; rw < 3; ++rw) {
            r.data[c][rw] = m.data[rw][c];
        }
    }

    // new translation = -Rᵀ · t, where t is m's translation column.
    const float tx = m.data[3][0];
    const float ty = m.data[3][1];
    const float tz = m.data[3][2];
    r.data[3][0] = -(r.data[0][0] * tx + r.data[1][0] * ty + r.data[2][0] * tz);
    r.data[3][1] = -(r.data[0][1] * tx + r.data[1][1] * ty + r.data[2][1] * tz);
    r.data[3][2] = -(r.data[0][2] * tx + r.data[1][2] * ty + r.data[2][2] * tz);

    return r;
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

/// @brief creates a camera POSE (camera-to-world transform) looking from eye
/// at center — the inverse of look_at(), built directly.
///
/// look_at() returns a VIEW matrix (world-to-eye); an entity's transform
/// component stores the opposite direction (entity-to-world). use this to
/// pose a camera entity: the renderer then derives the view matrix via
/// inverse_rigid(). building the pose directly (basis vectors as columns,
/// eye as translation) is cheaper and clearer than inverting look_at().
/// @param eye the position of the camera.
/// @param center the point the camera is looking at.
/// @param up the up vector (usually 0, 0, 1).
inline auto look_at_transform(const vec3& eye, const vec3& center,
                              const vec3& up) -> mat4 {
    vec3 const f = normalize(center - eye);  // forward (y)
    vec3 const s = normalize(cross(f, up));  // right (x)
    vec3 const u = cross(s, f);              // up (z)

    // columns are the camera's basis vectors expressed in world space,
    // translation is the camera position — exactly the transpose+negation
    // relationship to look_at()'s rows.
    mat4 m       = mat4::identity();
    m.data[0][0] = s.x;
    m.data[0][1] = s.y;
    m.data[0][2] = s.z;

    m.data[1][0] = f.x;
    m.data[1][1] = f.y;
    m.data[1][2] = f.z;

    m.data[2][0] = u.x;
    m.data[2][1] = u.y;
    m.data[2][2] = u.z;

    m.data[3][0] = eye.x;
    m.data[3][1] = eye.y;
    m.data[3][2] = eye.z;

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
