#pragma once
//
// vent public sdk.
// vertex structures.
// ——————————————————————
//
// defines the standard vertex layout used by the engine.

#include <_vent/vent_sdk.hpp>

#include <_vent/math/math.hpp>

#include <functional>

namespace vent {

/// @brief standard vertex structure for 3d meshes.
/// @note u and v are separate f32 members (not a vec2) because the vulkan vertex
/// attribute description reads the texcoord at offsetof(vertex, u) and relies on
/// v following it contiguously. keep them adjacent and in this order.
struct vertex {
    math::vec3 position;
    math::vec3 color;

    f32 u;
    f32 v;

    /// @brief exact (bitwise-value) equality. used only for de-duplicating
    /// vertices when loading meshes, so exact float comparison is intentional —
    /// two vertices are "the same" only if every component matches exactly.
    auto operator==(const vertex& other) const -> bool {
        return position.x == other.position.x &&
               position.y == other.position.y &&
               position.z == other.position.z && color.x == other.color.x &&
               color.y == other.color.y && color.z == other.color.z &&
               u == other.u && v == other.v;
    }
};

}  // namespace vent

// std::hash specialization for vertex.
// required so the obj loader can use std::unordered_map<vertex, u32> to collapse
// duplicate vertices into an index buffer. this is a de-dup hash, not a
// security/distribution-critical one — the xor-shift combine can collide on
// permuted components, which only costs an extra bucket probe during loading.
namespace std {
template <>
struct hash<vent::vertex> {
    auto operator()(vent::vertex const& vertex) const -> size_t {
        size_t h1 = hash<float>()(vertex.position.x) ^
                    (hash<float>()(vertex.position.y) << 1) ^
                    (hash<float>()(vertex.position.z) << 2);
        size_t h2 = hash<float>()(vertex.color.x) ^
                    (hash<float>()(vertex.color.y) << 1) ^
                    (hash<float>()(vertex.color.z) << 2);
        size_t h3 = hash<float>()(vertex.u) ^ (hash<float>()(vertex.v) << 1);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};
}  // namespace std
