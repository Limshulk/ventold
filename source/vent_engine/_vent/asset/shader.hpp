#pragma once
//
// vent public sdk.
// shader asset structure.
// ——————————————————————
//
// represents a compiled shader loaded into memory.

#include <_vent/vent_sdk.hpp>
#include <vector>

namespace vent {

/// @brief represents a loaded shader asset containing compiled bytecode.
struct shader_asset {
    std::vector<u32>
        spirv_bytecode;  ///< 0x00-0x18 (24b): the raw spirv bytecode.

    // reflection data can be added here later (e.g. descriptor set layouts).

    /// @brief check if the shader asset contains valid bytecode.
    /// @return true if valid, false otherwise.
    [[nodiscard]]
    auto is_valid() const -> bool {
        return !spirv_bytecode.empty();
    }
};

}  // namespace vent
