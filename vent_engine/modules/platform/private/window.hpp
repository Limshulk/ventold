#pragma once
//
// platform module.
// window class.
// ——————————————————————
// implements i_window using glfw.

#include <platform/interfaces/i_window.hpp>

#include <mutex>
#include <string>

// forward declare glfw types.
struct GLFWwindow;

namespace vent {

// --- window: glfw implementation ---
// —————————————————————————————————————————————————————————————————————————————
// note: this is not a system.

class platform_system;

/// @brief GLFW implementation of i_window.
class window final : public i_window {
public:
    /// @brief construct a window with the given description.
    explicit window(platform_system& platform, const window_desc& desc);
    ~window() override;

    VENT_NO_COPY_MOVE(window);

private:
    // --- internal state ---
    // —————————————————————————————————————————————————————————————————————————

    platform_system* _platform    = nullptr;    ///< owning platform system.
    GLFWwindow*      _glfw_window = nullptr;    ///< underlying glfw window.
    window_desc      _desc;                     ///< original creation desc.
    std::string      _title;                    ///< current title (mutable).
    u32              _width           = 0;      ///< current window width.
    u32              _height          = 0;      ///< current window height.
    u32              _fb_width        = 0;      ///< framebuffer width.
    u32              _fb_height       = 0;      ///< framebuffer height.
    bool             _is_main         = false;  ///< is this the main window?
    bool             _has_focus       = false;  ///< current focus state.
    bool             _close_requested = false;  ///< was close requested?
    bool             _size_changed = false;  ///< size changed since last poll?
    bool _focus_changed            = false;  ///< focus changed since last poll?
    // i_input*    _input           = nullptr;  ///< attached input system.

    mutable std::mutex _state_mutex;  ///< guards cached window state.

public:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create the underlying glfw window.
    /// @return true on success, false on failure.
    auto create() -> bool;

    /// @brief get the underlying glfw window handle.
    [[nodiscard]]
    auto get_glfw_window() const -> GLFWwindow* {
        return _glfw_window;
    }

    // --- ic_window implementation ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_title() const -> std::string_view override {
        std::lock_guard lock(_state_mutex);
        return _title;
    }
    [[nodiscard]]
    auto get_width() const -> u32 override {
        std::lock_guard lock(_state_mutex);
        return _width;
    }
    [[nodiscard]]
    auto get_height() const -> u32 override {
        std::lock_guard lock(_state_mutex);
        return _height;
    }
    [[nodiscard]]
    auto get_framebuffer_width() const -> u32 override {
        std::lock_guard lock(_state_mutex);
        return _fb_width;
    }
    [[nodiscard]]
    auto get_framebuffer_height() const -> u32 override {
        std::lock_guard lock(_state_mutex);
        return _fb_height;
    }
    [[nodiscard]]
    auto is_main() const -> bool override {
        std::lock_guard lock(_state_mutex);
        return _is_main;
    }
    [[nodiscard]]
    auto has_focus() const -> bool override {
        std::lock_guard lock(_state_mutex);
        return _has_focus;
    }
    [[nodiscard]]
    auto should_close() const -> bool override {
        std::lock_guard lock(_state_mutex);
        return _close_requested;
    }
    [[nodiscard]]
    auto get_style() const -> window_style override {
        return _desc.style;
    }
    [[nodiscard]]
    auto get_renderer() const -> renderer_api override {
        return _desc.renderer;
    }

    auto set_title(std::string_view title) -> void override;
    auto resize(u32 width, u32 height) -> void override;
    auto show() -> void override;
    auto hide() -> void override;
    auto request_close() -> void override;
    auto clear_close_request() -> void override;

    auto poll_size_changed(u32& out_width, u32& out_height) -> bool override;
    auto poll_focus_changed(bool& out_has_focus) -> bool override;

    [[nodiscard]]
    auto get_native_handle() const -> void* override;
    [[nodiscard]]
    auto get_native_display() const -> void* override;

    // --- i_window implementation ---
    // —————————————————————————————————————————————————————————————————————————

    auto set_main(bool is_main) -> void override {
        std::lock_guard lock(_state_mutex);
        _is_main = is_main;
    }
    // auto set_input(i_input* input) -> void override { _input = input; }

    // --- glfw handlers ---
    // —————————————————————————————————————————————————————————————————————————
    // called from static glfw callbacks via glfwGetWindowUserPointer.

    auto on_resize(int width, int height) -> void;
    auto on_focus(int focused) -> void;
    auto on_close_requested() -> void;
};

}  // namespace vent
