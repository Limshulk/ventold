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
struct vertex {
    math::vec3 position;
    math::vec3 color;

    f32 u;
    f32 v;

    bool operator==(const vertex& other) const {
        return position.x == other.position.x && position.y == other.position.y && position.z == other.position.z &&
               color.x == other.color.x && color.y == other.color.y && color.z == other.color.z &&
               u == other.u && v == other.v;
    }
};

}  // namespace vent

namespace std {
    template<> struct hash<vent::vertex> {
        size_t operator()(vent::vertex const& vertex) const {
            // simple hash combination
            size_t h1 = hash<float>()(vertex.position.x) ^ (hash<float>()(vertex.position.y) << 1) ^ (hash<float>()(vertex.position.z) << 2);
            size_t h2 = hash<float>()(vertex.color.x) ^ (hash<float>()(vertex.color.y) << 1) ^ (hash<float>()(vertex.color.z) << 2);
            size_t h3 = hash<float>()(vertex.u) ^ (hash<float>()(vertex.v) << 1);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}
