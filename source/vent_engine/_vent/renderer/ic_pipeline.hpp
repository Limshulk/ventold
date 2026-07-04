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

/// @brief description of a graphics pipeline.
struct pipeline_desc {
    shader_asset* shader =
        nullptr;  ///< 0x00-0x08 (8b): pointer to shader asset.
    std::string vertex_entry =
        "vertMain";  ///< 0x08-0x28 (32b): vertex shader entry point.
    std::string fragment_entry =
        "fragMain";  ///< 0x28-0x48 (32b): fragment shader entry point.
};

/// @brief interface for an opaque graphics pipeline object.
class ic_pipeline {
public:
    virtual ~ic_pipeline() = default;
};

}  // namespace vent
