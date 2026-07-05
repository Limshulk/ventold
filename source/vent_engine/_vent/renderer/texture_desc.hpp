#pragma once
//
// vent public sdk.
// texture descriptor.
// ——————————————————————
//
// defines the declarative configuration for creating a texture.

#include <_vent/vent_sdk.hpp>

#include <span>

namespace vent {

/// @brief declarative description to create a 2d texture.
struct texture_desc {
    u32                 width  = 0;
    u32                 height = 0;
    std::span<const u8> pixels;
};

}  // namespace vent
