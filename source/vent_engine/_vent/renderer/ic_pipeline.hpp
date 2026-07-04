#pragma once
//
// vent public sdk.
// pipeline interface.
// ——————————————————————
//
// abstract interface for a graphics pipeline.

#include <string>

namespace vent {

struct shader_asset;

struct pipeline_desc {
    shader_asset* shader = nullptr;
    std::string   vertex_entry = "vertMain";
    std::string   fragment_entry = "fragMain";
};

class ic_pipeline {
public:
    virtual ~ic_pipeline() = default;
};

} // namespace vent
