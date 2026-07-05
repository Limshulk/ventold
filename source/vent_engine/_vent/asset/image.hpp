#pragma once
//
// vent public sdk.
// image asset structure.
// ——————————————————————
//
// represents a loaded image asset.

#include <_vent/vent_sdk.hpp>

#include <vector>

namespace vent {

/// @brief represents a loaded image asset.
struct image_asset {
    u32             width    = 0;
    u32             height   = 0;
    u32             channels = 0;
    std::vector<u8> pixels;

    [[nodiscard]]
    auto is_valid() const -> bool {
        return !pixels.empty() && width > 0 && height > 0;
    }
};

}  // namespace vent
