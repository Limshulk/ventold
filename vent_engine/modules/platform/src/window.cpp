// platform module.
// window implementation.
// ——————————————————————
//

#include <window.hpp>

#include <platform_system.hpp>

#include <_vent/accessors.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// native access headers (for get_native_handle/display).
#if defined(__linux__)
    #define GLFW_EXPOSE_NATIVE_X11
    #define GLFW_EXPOSE_NATIVE_WAYLAND
    #include <GLFW/glfw3native.h>
#elif defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
    #include <GLFW/glfw3native.h>
#endif

namespace vent {

namespace {

auto glfw_framebuffer_size_callback(GLFWwindow* glfw_window,
                                    int         width,
                                    int         height) -> void {
    auto* win = static_cast<window*>(glfwGetWindowUserPointer(glfw_window));
    if (win) {
        win->on_resize(width, height);
    }
}

auto glfw_window_focus_callback(GLFWwindow* glfw_window, int focused) -> void {
    auto* win = static_cast<window*>(glfwGetWindowUserPointer(glfw_window));
    if (win) {
        win->on_focus(focused);
    }
}

auto glfw_window_close_callback(GLFWwindow* glfw_window) -> void {
    auto* win = static_cast<window*>(glfwGetWindowUserPointer(glfw_window));
    if (win) {
        win->on_close_requested();
    }
}

}  // namespace

window::window(platform_system& platform, const window_desc& desc)
    : _platform(&platform),
      _desc(desc),
      _title(desc.title),
      _width(desc.width),
      _height(desc.height),
      _fb_width(desc.width),
      _fb_height(desc.height) {
}

window::~window() {
    if (_glfw_window) {
        log()->trace(
            "platform", "destroying window: '{}' via destructor", _title);
        glfwDestroyWindow(_glfw_window);
        _glfw_window = nullptr;
    }
}

auto window::create() -> bool {
    glfwDefaultWindowHints();

    switch (_desc.renderer) {
        case renderer_api::opengl:
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            break;
        case renderer_api::vulkan:
        case renderer_api::dx12:
        case renderer_api::none:
        default: glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); break;
    }

    glfwWindowHint(GLFW_RESIZABLE,
                   has_style(_desc.style, window_style::resizable)
                       ? GLFW_TRUE
                       : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED,
                   has_style(_desc.style, window_style::has_titlebar)
                       ? GLFW_TRUE
                       : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,
                   has_style(_desc.style, window_style::topmost) ? GLFW_TRUE
                                                                 : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,
                   has_style(_desc.style, window_style::no_activate)
                       ? GLFW_FALSE
                       : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    _glfw_window = glfwCreateWindow(static_cast<int>(_width),
                                    static_cast<int>(_height),
                                    _title.c_str(),
                                    nullptr,
                                    nullptr);

    if (!_glfw_window) {
        log()->error("platform", "glfwCreateWindow failed for '{}'", _title);
        return false;
    }

    glfwSetWindowUserPointer(_glfw_window, this);
    glfwSetFramebufferSizeCallback(_glfw_window,
                                   glfw_framebuffer_size_callback);
    glfwSetWindowFocusCallback(_glfw_window, glfw_window_focus_callback);
    glfwSetWindowCloseCallback(_glfw_window, glfw_window_close_callback);

    if (_desc.centered && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                int x = (mode->width - static_cast<int>(_width)) / 2;
                int y = (mode->height - static_cast<int>(_height)) / 2;
                glfwSetWindowPos(_glfw_window, x, y);
            }
        }
    }

    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(_glfw_window, &fb_w, &fb_h);
    {
        std::lock_guard lock(_state_mutex);
        _fb_width  = static_cast<u32>(fb_w);
        _fb_height = static_cast<u32>(fb_h);
    }

    return true;
}

auto window::set_title(std::string_view title) -> void {
    auto title_copy = std::string(title);
    _platform->invoke_on_platform_thread(
        [this, title_copy = std::move(title_copy)]() mutable {
            GLFWwindow* glfw_window = nullptr;
            std::string window_title;
            {
                std::lock_guard lock(_state_mutex);
                _title       = std::move(title_copy);
                glfw_window  = _glfw_window;
                window_title = _title;
            }

            if (glfw_window) {
                glfwSetWindowTitle(glfw_window, window_title.c_str());
            }
        });
}

auto window::resize(u32 width, u32 height) -> void {
    _platform->invoke_on_platform_thread([this, width, height]() {
        GLFWwindow* glfw_window   = nullptr;
        u32         target_width  = 0;
        u32         target_height = 0;
        {
            std::lock_guard lock(_state_mutex);
            _width        = width;
            _height       = height;
            glfw_window   = _glfw_window;
            target_width  = _width;
            target_height = _height;
        }

        if (glfw_window) {
            glfwSetWindowSize(glfw_window,
                              static_cast<int>(target_width),
                              static_cast<int>(target_height));
        }
    });
}

auto window::show() -> void {
    _platform->invoke_on_platform_thread([this]() {
        GLFWwindow* glfw_window = nullptr;
        {
            std::lock_guard lock(_state_mutex);
            glfw_window = _glfw_window;
            _has_focus  = true;
        }

        if (glfw_window) {
            glfwShowWindow(glfw_window);
        }
    });
}

auto window::hide() -> void {
    _platform->invoke_on_platform_thread([this]() {
        GLFWwindow* glfw_window = nullptr;
        {
            std::lock_guard lock(_state_mutex);
            glfw_window = _glfw_window;
        }

        if (glfw_window) {
            glfwHideWindow(glfw_window);
        }
    });
}

auto window::request_close() -> void {
    _platform->invoke_on_platform_thread([this]() {
        GLFWwindow* glfw_window = nullptr;
        {
            std::lock_guard lock(_state_mutex);
            _close_requested = true;
            glfw_window      = _glfw_window;
        }

        if (glfw_window) {
            glfwSetWindowShouldClose(glfw_window, GLFW_TRUE);
        }
    });
}

auto window::clear_close_request() -> void {
    _platform->invoke_on_platform_thread([this]() {
        GLFWwindow* glfw_window = nullptr;
        {
            std::lock_guard lock(_state_mutex);
            _close_requested = false;
            glfw_window      = _glfw_window;
        }

        if (glfw_window) {
            glfwSetWindowShouldClose(glfw_window, GLFW_FALSE);
        }
    });
}

auto window::poll_size_changed(u32& out_width, u32& out_height) -> bool {
    std::lock_guard lock(_state_mutex);
    if (_size_changed) {
        out_width     = _fb_width;
        out_height    = _fb_height;
        _size_changed = false;
        return true;
    }
    return false;
}

auto window::poll_focus_changed(bool& out_has_focus) -> bool {
    std::lock_guard lock(_state_mutex);
    if (_focus_changed) {
        out_has_focus  = _has_focus;
        _focus_changed = false;
        return true;
    }
    return false;
}

auto window::get_native_handle() const -> void* {
    std::lock_guard lock(_state_mutex);
#if defined(__linux__)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return glfwGetWaylandWindow(_glfw_window);
    }
    return reinterpret_cast<void*>(glfwGetX11Window(_glfw_window));
#elif defined(_WIN32)
    return glfwGetWin32Window(_glfw_window);
#elif defined(__APPLE__)
    return glfwGetCocoaWindow(_glfw_window);
#else
    return nullptr;
#endif
}

auto window::get_native_display() const -> void* {
    std::lock_guard lock(_state_mutex);
#if defined(__linux__)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        return glfwGetWaylandDisplay();
    }
    return glfwGetX11Display();
#elif defined(_WIN32)
    return GetModuleHandle(nullptr);
#else
    return nullptr;
#endif
}

auto window::on_resize(int width, int height) -> void {
    std::string title;
    {
        std::lock_guard lock(_state_mutex);
        _fb_width     = static_cast<u32>(width);
        _fb_height    = static_cast<u32>(height);
        _size_changed = true;
        title         = _title;
    }

    log()->trace(
        "platform", "window '{}' resized: {}x{}", title, width, height);
}

auto window::on_focus(int focused) -> void {
    std::string title;
    bool        has_focus = false;
    {
        std::lock_guard lock(_state_mutex);
        _has_focus     = (focused == GLFW_TRUE);
        _focus_changed = true;
        title          = _title;
        has_focus      = _has_focus;
    }

    log()->trace("platform",
                 "window '{}': focus {}",
                 title,
                 has_focus ? "gained" : "lost");
}

auto window::on_close_requested() -> void {
    std::string title;
    {
        std::lock_guard lock(_state_mutex);
        _close_requested = true;
        title            = _title;
    }

    log()->trace("platform", "window '{}' close requested", title);
}

}  // namespace vent
