#pragma once
//
// vent public sdk.
// client facing asset_manager interface for public use.
// ——————————————————————
//
// provides public access to mounting physical paths to the virtual file system
// and resolving virtual paths to physical directories.

#include <_vent/vent_sdk.hpp>
#include <_vent/asset/shader.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vent {

class ic_asset {
public:
    virtual ~ic_asset() = default;

    static constexpr std::string_view system_name = "vent.system.asset";

    /// @brief mounts a physical directory to a virtual path.
    virtual auto mount(std::string_view protocol,
                       std::string_view physical_path) -> void = 0;

    /// @brief resolves a virtual path to a physical path.
    [[nodiscard]]
    virtual auto resolve(std::string_view virtual_path) const
        -> std::string = 0;

    /// @brief reads a file from disk into a byte buffer.
    [[nodiscard]]
    virtual auto read_binary_file(std::string_view virtual_path) const
        -> std::vector<u8> = 0;

    /// @brief loads a shader from a compiled spirv binary into the asset cache.
    virtual auto load_shader(std::string_view virtual_path)
        -> shader_asset* = 0;

    /// @brief releases a shader from the cache.
    virtual auto release_shader(shader_asset* asset) -> void = 0;
};

}  // namespace vent