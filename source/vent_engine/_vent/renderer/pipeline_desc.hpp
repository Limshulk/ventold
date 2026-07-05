#pragma once
//
// vent public sdk.
// graphics pipeline descriptor.
// ——————————————————————

#include <string>

namespace vent {

struct shader_asset;

/// @brief describes a graphics pipeline to be created by the renderer.
struct pipeline_desc {
    const shader_asset* shader         = nullptr;
    std::string         vertex_entry   = "vertMain";
    std::string         fragment_entry = "fragMain";
};

}  // namespace vent
