#pragma once
//
// vent public sdk.
// pipeline description.
// ——————————————————————
//
// describes a graphics pipeline to be created by the renderer backend.

#include <string>

namespace vent {

struct shader_asset;

struct pipeline_desc {
    shader_asset* shader = nullptr;
    std::string   vertex_entry = "vertMain";
    std::string   fragment_entry = "fragMain";
};

} // namespace vent
