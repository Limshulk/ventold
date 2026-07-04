#pragma once
//
// asset module.
// shader asset structure.
// ——————————————————————
//
// represents a compiled shader loaded into memory.

#include <_vent/vent_sdk.hpp>
#include <vector>

namespace vent {

struct shader_asset {
    std::vector<u32>   spirv_bytecode;

    // reflection data can be added here later (e.g. descriptor set layouts).
    
    [[nodiscard]] auto is_valid() const -> bool {
        return !spirv_bytecode.empty();
    }
};

} // namespace vent
