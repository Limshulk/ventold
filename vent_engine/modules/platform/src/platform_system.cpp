// platform module.
// platform implementation.
// ——————————————————————
//

#include <platform_system.hpp>
#include <window.hpp>

#include <_vent/accessors.hpp>

#include <core/thread_registry.hpp>

#include <core/system/registration.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstring>

#ifdef __linux__
    #include <pthread.h>
#endif

namespace vent {

// --- glfw callback ---
// —————————————————————————————————————————————————————————————————————————————

namespace {

auto glfw_error_callback(int error, const char* description) -> void {
    log()->error("platform", "glfw error {}: {}", error, description);
}

}  // namespace

// --- platform implementation ---
// —————————————————————————————————————————————————————————————————————————————

auto platform_system::initialize() -> bool {

    log()->trace("platform", "initializing platform system...");
    log()->trace("platform", "spawning dedicated platform thread...");

    std::promise<bool> ready;
    auto               future = ready.get_future();

    _running.store(true, std::memory_order_release);

    _platform_thread = std::thread([this, ready = std::move(ready)]() mutable {
        platform_thread_main(std::move(ready));
    });

    if (!future.get()) {
        _running.store(false, std::memory_order_release);
        if (_platform_thread.joinable()) {
            _platform_thread.join();
        }
        return false;
    }

    return true;
}

auto platform_system::shutdown() -> void {
    log()->trace("platform", "shutting down platform system...");

    _running.store(false, std::memory_order_release);
    glfwPostEmptyEvent();

    if (_platform_thread.joinable() &&
        _platform_thread.get_id() != std::this_thread::get_id()) {
        _platform_thread.join();
    }

    if (_platform_thread.joinable()) {
        _platform_thread.detach();
    }

    log()->trace("platform", "platform shutdown complete");
}

auto platform_system::is_platform_thread() const -> bool {
    return std::this_thread::get_id() == _platform_thread_id;
}

auto platform_system::enqueue_command(std::function<void()> command) -> void {
    if (!command) {
        return;
    }

    {
        std::lock_guard lock(_command_mutex);
        _commands.push(std::move(command));
    }

    glfwPostEmptyEvent();
}

auto platform_system::process_commands() -> void {
    std::queue<std::function<void()>> commands;
    {
        std::lock_guard lock(_command_mutex);
        std::swap(commands, _commands);
    }

    while (!commands.empty()) {
        commands.front()();
        commands.pop();
    }
}

auto platform_system::process_close_requests() -> void {
    std::vector<window*> destroy_list;
    auto                 add_destroy = [&destroy_list](window* candidate) {
        if (!candidate) {
            return;
        }
        if (std::find(destroy_list.begin(), destroy_list.end(), candidate) ==
            destroy_list.end()) {
            destroy_list.push_back(candidate);
        }
    };

    std::string main_title;
    bool        main_should_close = false;
    bool        has_other_windows = false;

    {
        std::lock_guard lock(_state_mutex);

        for (const auto& window_ptr : _windows) {
            if (!window_ptr) {
                continue;
            }

            if (window_ptr.get() == _main_window) {
                main_title        = std::string(window_ptr->get_title());
                main_should_close = window_ptr->should_close();
            } else {
                has_other_windows = true;
                if (window_ptr->should_close()) {
                    add_destroy(window_ptr.get());
                }
            }
        }

        if (main_should_close) {
            auto policy = _close_policy.load(std::memory_order_acquire);

            switch (policy) {
                case window_close_policy::exit_on_main_close:
                    log()->trace("platform",
                                 "main window closed; destroying all windows");
                    for (const auto& window_ptr : _windows) {
                        add_destroy(window_ptr.get());
                    }
                    break;

                case window_close_policy::keep_running_until_all_closed:
                    if (has_other_windows) {
                        log()->trace("platform",
                                     "main window '{}' close ignored because "
                                     "other windows are still open",
                                     main_title);
                        _main_window->clear_close_request();
                    } else {
                        add_destroy(_main_window);
                    }
                    break;
            }
        }
    }

    for (auto* window_ptr : destroy_list) {
        destroy_window(window_ptr);
    }
}

auto platform_system::platform_thread_main(std::promise<bool> ready) -> void {
    // register the platform thread so logs and diagnostics can identify it.
    thread_registry::register_thread("P:UI");

    _platform_thread_id = std::this_thread::get_id();

    log()->trace("platform", "platform thread started");

    // set error callback before init.
    glfwSetErrorCallback(glfw_error_callback);

    // check for backend override via environment variable.
    // default to x11 on linux unless explicitly overridden.
    const char* backend_env = std::getenv("VENT_DISPLAY_BACKEND");
    if (backend_env) {
        if (std::strcmp(backend_env, "wayland") == 0) {
            log()->info("platform",
                        "forcing wayland backend via VENT_DISPLAY_BACKEND");
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        } else if (std::strcmp(backend_env, "x11") == 0) {
            log()->info("platform",
                        "forcing x11 backend via VENT_DISPLAY_BACKEND");
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        }
    } else {
        // default to x11 on linux.
#ifdef __linux__
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    }

    // initialize glfw.
    if (!glfwInit()) {
        log()->error("platform", "glfwInit() failed.");
        ready.set_value(false);
        return;
    }

    _glfw_initialized = true;

    // log which platform we're running on.
    int         platform     = glfwGetPlatform();
    const char* platform_str = "unknown";
    switch (platform) {
        case GLFW_PLATFORM_WAYLAND: platform_str = "wayland"; break;
        case GLFW_PLATFORM_X11: platform_str = "x11"; break;
        case GLFW_PLATFORM_WIN32: platform_str = "win32"; break;
        case GLFW_PLATFORM_COCOA: platform_str = "cocoa"; break;
    }
    log()->trace(
        "platform", "platform initialized (backend: {}).", platform_str);

    ready.set_value(true);

    // blocking main platform loop.
    while (_running.load(std::memory_order_acquire)) {
        process_commands();
        glfwWaitEventsTimeout(0.016);
        process_close_requests();
    }

    // empty command queue before exiting.
    process_commands();

    {
        std::lock_guard lock(_state_mutex);
        _windows.clear();
        _main_window = nullptr;
    }

    // terminate glfw.
    if (_glfw_initialized) {
        glfwTerminate();
        _glfw_initialized = false;
    }

    log()->trace("platform", "platform thread stopped.");
    thread_registry::unregister_thread();
}

auto platform_system::get_platform_type() const -> platform_type {
    // big insight from debugging: don't just assume stuff based on compile-time
    // platform macros. either they are broken in some cases or i am lost.
    int platform = glfwGetPlatform();
    switch (platform) {
        case GLFW_PLATFORM_WAYLAND: return platform_type::wayland;
        case GLFW_PLATFORM_X11: return platform_type::x11;
        case GLFW_PLATFORM_WIN32: return platform_type::win32;
        case GLFW_PLATFORM_COCOA: return platform_type::cocoa;
        default: return platform_type::unknown;
    }
}

auto platform_system::create_window(const window_desc& desc) -> ic_window* {
    log()->trace("platform",
                 "creating window '{}' ({}x{}) on platform thread.",
                 desc.title,
                 desc.width,
                 desc.height);

    return invoke_on_platform_thread([this, desc]() -> ic_window* {
        auto win = std::make_unique<window>(*this, desc);

        if (!win->create()) {
            log()->error(
                "platform", "failed to create window '{}'.", desc.title);
            return nullptr;
        }

        std::lock_guard lock(_state_mutex);

        // first window becomes main window.
        if (_windows.empty()) {
            win->set_main(true);
            _main_window = win.get();
            log()->trace(
                "platform", "window '{}' set as main window.", desc.title);
        }

        ic_window* handle = win.get();
        _windows.push_back(std::move(win));

        // todo: publish window.created event for listeners (e.g., renderer
        // todo: staged init).
        // window_created_event event_data{.window = handle};
        // events()->publish("window.created", &event_data);

        log()->trace("platform", "window created: '{}'.", desc.title);
        return handle;
    });
}

auto platform_system::destroy_window(ic_window* handle) -> void {
    if (!handle)
        return;

    invoke_on_platform_thread([this, handle]() {
        std::unique_ptr<window> destroyed_window;
        window*                 promoted_main = nullptr;
        std::string             title;

        {
            std::lock_guard lock(_state_mutex);

            auto it = std::find_if(
                _windows.begin(), _windows.end(), [handle](const auto& win) {
                    return win.get() == handle;
                });

            if (it == _windows.end()) {
                log()->trace("platform",
                             "destroy_window: window already gone, ignoring.");
                return;
            }

            bool was_main = (handle == _main_window);
            title         = std::string((*it)->get_title());

            if (was_main) {
                if (_windows.size() > 1) {
                    promoted_main =
                        (it == _windows.begin())
                            ? (_windows.size() > 1 ? _windows[1].get()
                                                   : nullptr)
                            : _windows.front().get();
                    _main_window = promoted_main;
                } else {
                    _main_window = nullptr;
                }
            }

            destroyed_window = std::move(*it);
            _windows.erase(it);
        }

        if (promoted_main) {
            promoted_main->set_main(true);
            log()->trace("platform",
                         "promoted '{}' to main window",
                         promoted_main->get_title());
        }

        destroyed_window.reset();

        log()->trace("platform", "window destroyed: '{}'.", title);
    });
}

auto platform_system::get_main_window() const -> ic_window* {
    std::lock_guard lock(_state_mutex);
    return _main_window;
}

auto platform_system::get_window_count() const -> u32 {
    std::lock_guard lock(_state_mutex);
    return static_cast<u32>(_windows.size());
}

auto platform_system::set_close_policy(window_close_policy policy) -> void {
    _close_policy.store(policy, std::memory_order_release);
    log()->trace(
        "platform", "window close policy set to {}.", static_cast<int>(policy));
}

auto platform_system::poll_events() -> void {
    invoke_on_platform_thread([]() {
        glfwPollEvents();
    });
}

auto platform_system::get_internal_window(ic_window* handle) -> i_window* {
    return static_cast<i_window*>(handle);
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::platform_system,
                     vent::i_platform,
                     vent::ic_platform);