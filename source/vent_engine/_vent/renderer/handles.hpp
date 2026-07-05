#pragma once
//
// vent public sdk.
// renderer handle declarations.
// ——————————————————————
//
// details.

#include <_vent/vent_sdk.hpp>

namespace vent {

// render object handles.
using pipeline_handle                             = u64;
constexpr pipeline_handle INVALID_PIPELINE_HANDLE = 0;

using mesh_handle                         = u64;
constexpr mesh_handle INVALID_MESH_HANDLE = 0;

using texture_handle                            = u64;
constexpr texture_handle INVALID_TEXTURE_HANDLE = 0;

}  // namespace vent