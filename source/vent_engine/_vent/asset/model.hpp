#pragma once
//
// vent public sdk.
// model asset structure.
// ——————————————————————
//
// defines the structure of a loaded 3d model asset.

#include <_vent/vent_sdk.hpp>
#include <_vent/renderer/vertex.hpp>

#include <vector>

namespace vent {

/// @brief represents a loaded 3d model.
struct model_asset {
    std::vector<vertex> vertices;
    std::vector<u32>    indices;
};

}  // namespace vent
