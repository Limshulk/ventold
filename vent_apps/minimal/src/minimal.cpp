// minimal client application.
// ——————————————————————
//
// demonstrates a basic vent client using the client_base class.

#include <_vent/_vent.hpp>

class minimal_client : public vent::client_base {
public:
    // --- ir_dependencies ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief declare systems this client depends on.
    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {"vent.platform_system"};
        return deps;
    }

    // --- client_base ---
    // —————————————————————————————————————————————————————————————————————————

    auto on_initialize() -> bool override {
        vent::log()->info("client", "minimal_client starting!");

        // optional: subscribe to resize events for swapchain recreation.
        // _resize_sub = vent::events()->subscribe("window.resized",
        //     [](std::string_view /*event*/, void* data) {
        //         auto* e = static_cast<vent::window_resize_event*>(data);
        //         vent::log()->info("client", "window resized to {}x{}",
        //         e->width, e->height);
        //     });

        // create three windows.
        _windows.clear();
        _windows.reserve(3);

        for (std::size_t i = 0; i < 3; ++i) {
            vent::window_desc desc;
            desc.title    = i == 0 ? "vent - minimal client (main)"
                                   : std::string("vent - auxiliary window ") +
                                      std::to_string(i);
            desc.width    = 1280;
            desc.height   = 720;
            desc.style    = vent::window_style::standard;
            desc.renderer = vent::renderer_api::vulkan;

            auto* window = vent::platform()->create_window(desc);
            if (!window) {
                vent::log()->error("client", "failed to create window {}", i);
                return false;
            }

            window->show();
            _windows.push_back(window);
            vent::log()->info("client",
                              "created window {} of 3: '{}'",
                              i + 1,
                              window->get_title());
        }

        vent::log()->info("client",
                          "multi-window test initialized with {} windows",
                          _windows.size());

        _frame_count = 0;
        _elapsed     = 0.0;
        return true;
    }

    auto on_update(vent::f64 delta_time) -> void override {
        _frame_count++;
        _elapsed += delta_time;

        // note: no close handling needed here. engine detects main window
        //       close and exits the loop automatically.
    }

    auto on_shutdown() -> void override {
        // unsubscribe from events (cleanup).
        // if (_resize_sub != vent::INVALID_SUBSCRIPTION) {
        //     vent::events()->unsubscribe(_resize_sub);
        // }

        for (auto it = _windows.rbegin(); it != _windows.rend(); ++it) {
            vent::platform()->destroy_window(*it);
        }
        _windows.clear();

        vent::log()->info("client",
                          "goodbye! total frames: {}, avg fps: {:.1f}",
                          _frame_count,
                          _frame_count / _elapsed);
    }

    [[nodiscard]]
    auto close_policy() const -> vent::window_close_policy override {
        return vent::window_close_policy::exit_on_main_close;
    }

private:
    std::vector<vent::ic_window*> _windows;
    vent::u64                     _frame_count = 0;
    vent::f64                     _elapsed     = 0.0;
};

VENT_REGISTER_CLIENT(minimal_client);