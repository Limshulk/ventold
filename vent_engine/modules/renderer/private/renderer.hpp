#pragma once
//
// renderer module.
// renderer frontend.
// ——————————————————————
//
// implements the renderer frontend inheriting i_renderer. the frontend
// processes all rendering.

#include <renderer/interfaces/i_renderer.hpp>
#include <renderer/interfaces/i_render_backend.hpp>

#include <_vent/accessors.hpp>
#include <_vent/system/system_base.hpp>
#include <_vent/interfaces/ir_dependencies.hpp>

namespace vent {

// --- forward declarations ---
class i_device;
class i_window;

class renderer_system final
    : public system_base
    , public i_renderer
    , public ir_dependencies {
public:
    renderer_system()           = default;
    ~renderer_system() override = default;

    VENT_NO_COPY_MOVE(renderer_system);

public:
    // --- system_base implementations ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return "vent.renderer_system";
    }

    [[nodiscard]]
    auto on_initialization(i32 stage = 0) -> initialization_result override {
        log()->trace("renderer", "renderer_system::on_initialization(stage={})", stage);
        switch(stage) {
            case 0: return initialize_s0();
            default: return initialization_result::failed();
        }
    }

    auto on_shutdown() -> void override { shutdown(); }

    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {"vent.platform_system"};
        return deps;
    }

private:
    // --- targets ---
    // —————————————————————————————————————————————————————————————————————————

    i_render_backend* _backend = nullptr;  ///< backend from system_registry.
    i_window*         _window  = nullptr;  ///< target window for rendering.
    i_device*         _device  = nullptr;  ///< gpu device.


private:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    // --- initialization stages ---
    
    /// @brief initialization stage 0: create render backend.
    /// @return on success: await_event("vent.window.created").
    auto initialize_s0()
        -> initialization_result;  ///< stage 0: create render backend.

    /// @brief shutdown renderer system.
    auto shutdown() -> void;
};

}  // namespace vent