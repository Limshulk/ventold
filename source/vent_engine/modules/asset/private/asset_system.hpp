#pragma once
//
// asset module.
// asset manager system.
// ——————————————————————
//
// provides virtual file system capabilities.

#include <asset/interfaces/i_asset.hpp>

#include <_vent/core/ir_dependencies.hpp>
#include <_vent/log/ic_log.hpp>

#include <_vent/asset/shader.hpp>
#include <_vent/system/system_base.hpp>

#include <unordered_map>
#include <string>
#include <memory>

namespace vent {

class asset_system final
    : public system_base
    , public i_asset
    , public ir_dependencies {
public:
    asset_system()           = default;
    ~asset_system() override = default;

    VENT_NO_COPY_MOVE(asset_system);

public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    // asset is a non-bootstrap system.

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_asset::system_name;
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override {
        return initialize() ? system_initialization_status::success
                            : system_initialization_status::failed;
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    /// @brief initialize the asset system.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief shutdown.
    auto shutdown() -> void;

public:
    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {ic_log::system_name};
        return deps;
    }

    auto mount(std::string_view protocol, std::string_view physical_path)
        -> void override;

    [[nodiscard]]
    auto resolve(std::string_view virtual_path) const -> std::string override;

    [[nodiscard]]
    auto read_binary_file(std::string_view virtual_path) const
        -> std::vector<u8> override;

    auto load_shader(std::string_view virtual_path) -> shader_asset* override;
    auto release_shader(shader_asset* asset) -> void override;

    auto load_image(std::string_view virtual_path) -> image_asset* override;
    auto release_image(image_asset* asset) -> void override;

    auto load_model(std::string_view virtual_path) -> model_asset* override;
    auto release_model(model_asset* asset) -> void override;

private:
    std::unordered_map<std::string, std::string> _mount_points;
    std::unordered_map<std::string, std::unique_ptr<shader_asset>> _shader_cache;
    std::mutex _shader_mutex;

    std::unordered_map<std::string, std::unique_ptr<image_asset>> _image_cache;
    std::mutex                                                    _image_mutex;

    std::unordered_map<std::string, std::unique_ptr<model_asset>> _model_cache;
    std::mutex                                                    _model_mutex;
};

}  // namespace vent
