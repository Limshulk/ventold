#pragma once
//
// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

#include <_vent/system/system_base.hpp>

#include <renderer/interfaces/i_render_backend.hpp>

#include <vulkan/vulkan_raii.hpp>

namespace vent {

class vulkan_backend final
    : public system_base
    , public i_render_backend {
public:
    vulkan_backend()           = default;
    ~vulkan_backend() override = default;

    VENT_NO_COPY_MOVE(vulkan_backend);
public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return "vent.vulkan_backend";
    }

    [[nodiscard]]
    auto on_initialization(i32 stage = 0) -> initialization_result override {
        return initialize() ? initialization_result::complete()
                            : initialization_result::failed();
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    // --- vulkan raii ---
    // —————————————————————————————————————————————————————————————————————————
    // these are raii objects. they are declared in initialization order and
    // destroyed in reverse order automatically via raii.

    vk::raii::Context  _context;  ///< vulkan function loader context.
    vk::raii::Instance _instance = nullptr;  ///< vulkan instance.

public:
    // --- i_render_backend ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_api_name() const -> std::string_view override {
        return "vulkan";
    }

private:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief perform vulkan initialization.
    /// @return true if initialization succeeded, false if it failed.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief perform vulkan shutdown and cleanup.
    auto shutdown() -> void;

    // --- vulkan initialization ---

    /// @brief create the vulkan instance.
    /// @return true if instance creation succeeded, false if it failed.
    [[nodiscard]]
    auto create_instance() -> bool;
};

}  // namespace vent
