#pragma once
//
// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

#include <vulkan_swapchain.hpp>

#include <_vent/system/system_base.hpp>
#include <_vent/interfaces/ic_window.hpp>

#include <renderer/interfaces/i_render_backend.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <vector>

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
    auto on_initialization(i32 stage = 0)
        -> system_initialization_result override {
        return initialize() ? system_initialization_result::complete()
                            : system_initialization_result::failed();
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    bool _validation_enabled = false;  ///< are validation layers active?

    // --- vulkan raii ---
    // —————————————————————————————————————————————————————————————————————————
    // vulkan raii objects. they are declared in initialization order and
    // automatically destroyed in reverse.

    vk::raii::Context  _context {};  ///< vulkan function loader context.
    vk::raii::Instance _instance = nullptr;  ///< vulkan instance.
    vk::raii::DebugUtilsMessengerEXT _debug_messenger =
        nullptr;  ///< validation layer messenger.
    vk::raii::PhysicalDevice _physical_device = nullptr;  ///< selected gpu.
    vk::raii::Device         _device          = nullptr;  ///< logical device.
    vk::raii::Queue          _graphics_queue  = nullptr;  ///< graphics queue.

    u32 _graphics_queue_family = 0;  ///< graphics queue family index.

    // --- per-window surfaces ---
    // —————————————————————————————————————————————————————————————————————————
    // surfaces are created per-window and stored alongside the window pointer.

    struct window_surface {
        ic_window*                        window = nullptr;
        std::unique_ptr<vulkan_swapchain> swapchain;
        u32                               present_queue_family = 0;
        vk::raii::Queue                   present_queue        = nullptr;
    };
    std::vector<window_surface> _surfaces;
    std::shared_mutex           _mutex;

public:
    // --- i_render_backend ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_api_name() const -> std::string_view override {
        return "vulkan";
    }

    auto create_surface(ic_window* window) -> bool override;
    auto destroy_surface(ic_window* window) -> void override;

    auto set_frames_in_flight(ic_window* window, u32 count) -> void override;
    auto begin_frame(ic_window* window) -> bool override;
    auto end_frame(ic_window* window) -> void override;

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

    /// @brief create the vulkan instance with extensions and validation layers.
    /// @return true if instance creation succeeded, false if it failed.
    [[nodiscard]]
    auto create_instance() -> bool;

    /// @brief set up the debug messenger for validation layer output.
    /// @return true on success, false on failure. always true when validation
    /// is disabled.
    [[nodiscard]]
    auto setup_debug_messenger() -> bool;

    /// @brief select the best physical device (gpu).
    /// @return true if a suitable device was found, false otherwise.
    [[nodiscard]]
    auto pick_physical_device() -> bool;

    /// @brief create the logical device and retrieve queue handles.
    /// @return true on success, false on failure.
    [[nodiscard]]
    auto create_logical_device() -> bool;

    // --- surface helpers ---

    /// @brief create a platform-specific vulkan surface for a window.
    /// @param window the window to create a surface for.
    /// @return raii surface handle, or nullptr on failure.
    [[nodiscard]]
    auto create_surface_for_window(ic_window* window) -> vk::raii::SurfaceKHR;
};

}  // namespace vent
