#pragma once
//
// asset module.
// asset manager system.
// ——————————————————————
//
// provides virtual file system capabilities.

#include <asset/interfaces/i_asset.hpp>

#include <_vent/interfaces/ic_log.hpp>
#include <_vent/interfaces/ir_dependencies.hpp>

#include <_vent/shader_asset.hpp>
#include <_vent/system/system_base.hpp>

#include <unordered_map>
#include <string>
#include <memory>

namespace vent {

class asset_system final
    : public system_base
    , public i_asset
    , public ir_dependencies {
    VENT_NO_COPY_MOVE(asset_system);
public:
    asset_system() = default;

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_asset::system_name;
    }

    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {ic_log::system_name};
        return deps;
    }

    [[nodiscard]]
    auto on_initialization(i32 stage) -> system_initialization_result override;

    auto on_shutdown() -> void override;

    auto mount(std::string_view protocol, std::string_view physical_path)
        -> void override;

    [[nodiscard]]
    auto resolve(std::string_view virtual_path) const -> std::string override;

    [[nodiscard]]
    auto read_binary_file(std::string_view virtual_path) const
        -> std::vector<u8> override;

    auto load_shader(std::string_view virtual_path)
        -> shader_asset* override;

    auto release_shader(shader_asset* asset) -> void override;

private:
    std::unordered_map<std::string, std::string> _mount_points;
    std::unordered_map<std::string, std::unique_ptr<shader_asset>> _shader_cache;
};

}  // namespace vent
