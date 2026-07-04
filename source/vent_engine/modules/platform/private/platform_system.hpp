#pragma once
//
// platform module.
// platform implementation.
// ——————————————————————
//
// implements i_platform using glfw.

#include <platform/interfaces/i_platform.hpp>

#include <_vent/system/system_base.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace vent {

// forward declarations.
class window;

class platform_system final
    : public system_base
    , public i_platform {
public:
    platform_system()           = default;
    ~platform_system() override = default;

    VENT_NO_COPY_MOVE(platform_system);

public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    // platform is a non-bootstrap system.

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_platform::system_name;
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override {
        return initialize() ? system_initialization_status::success
                            : system_initialization_status::failed;
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    // --- internal state ---
    // —————————————————————————————————————————————————————————————————————————

    bool              _glfw_initialized = false;  ///< glfw init state.
    std::thread       _platform_thread;           ///< native platform thread.
    std::thread::id   _platform_thread_id {};     ///< owning thread id.
    std::atomic<bool> _running {false};           ///< platform loop state.

    mutable std::mutex                _state_mutex;  ///< guards window state.
    std::mutex                        _command_mutex;
    std::queue<std::function<void()>> _commands;
    std::vector<std::unique_ptr<window>> _windows;  ///< owned windows.
    window*                          _main_window = nullptr;  ///< main window.
    std::atomic<window_close_policy> _close_policy {
        window_close_policy::exit_on_main_close};

    /// @brief initialize the platform system.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief shutdown.
    auto shutdown() -> void;

public:
    // --- ic_platform ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_platform_type() const -> platform_type override;

    [[nodiscard]]
    auto get_window_system_extensions(renderer_api api) const
        -> std::vector<const char*> override;

    [[nodiscard]]
    auto create_window(const window_desc& desc) -> ic_window* override;
    auto destroy_window(ic_window* window) -> void override;
    [[nodiscard]]
    auto get_main_window() const -> ic_window* override;
    [[nodiscard]]
    auto get_window_count() const -> u32 override;
    [[nodiscard]]
    auto get_windows() const -> std::vector<ic_window*> override;
    auto set_close_policy(window_close_policy policy) -> void override;
    auto poll_events() -> void override;

    // --- i_platform ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_internal_window(ic_window* window) -> i_window* override;

    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief true if called from the platform thread.
    [[nodiscard]]
    auto is_platform_thread() const -> bool;

    /// @brief enqueue a command for execution on the platform thread.
    auto enqueue_command(std::function<void()> command) -> void;

    /// @brief execute a callable on the platform thread and wait for the
    /// result.
    template <typename F>
    auto invoke_on_platform_thread(F&& command) -> std::invoke_result_t<F&> {
        using result_t  = std::invoke_result_t<F&>;
        using command_t = std::decay_t<F>;

        if (is_platform_thread()) {
            if constexpr (std::is_void_v<result_t>) {
                std::forward<F>(command)();
                return;
            } else {
                return std::forward<F>(command)();
            }
        }

        auto command_ptr =
            std::make_shared<command_t>(std::forward<F>(command));
        auto promise_ptr = std::make_shared<std::promise<result_t>>();
        auto future      = promise_ptr->get_future();

        enqueue_command([command_ptr, promise_ptr]() mutable {
            if constexpr (std::is_void_v<result_t>) {
                (*command_ptr)();
                promise_ptr->set_value();
            } else {
                promise_ptr->set_value((*command_ptr)());
            }
        });

        if constexpr (std::is_void_v<result_t>) {
            future.get();
            return;
        } else {
            return future.get();
        }
    }

private:
    /// @brief entry point for the native platform thread.
    auto platform_thread_main(std::promise<bool> ready) -> void;

    /// @brief drain queued commands on the platform thread.
    auto process_commands() -> void;

    /// @brief destroy or preserve windows based on their close state and the
    /// configured client policy.
    auto process_close_requests() -> void;
};

}  // namespace vent
