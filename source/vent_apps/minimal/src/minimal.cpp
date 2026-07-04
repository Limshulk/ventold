// minimal client application.
// ——————————————————————
//
// demonstrates a basic vent client using the client_base class.

#include <_vent/_vent.hpp>

#include <_vent/shader_asset.hpp>

#include <_vent/interfaces/ic_pipeline.hpp>
#include <_vent/interfaces/pipeline_desc.hpp>

#include <memory>

class minimal_client : public vent::client_base {
public:
    // --- ir_dependencies ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief declare systems this client depends on.
    [[nodiscard]]
    auto dependencies() const -> std::span<const std::string_view> override {
        static constexpr std::string_view deps[] = {
            vent::ic_platform::system_name,
            vent::ic_renderer::system_name,
            vent::ic_asset::system_name};
        return deps;
    }

    // --- client_base ---
    // —————————————————————————————————————————————————————————————————————————

    auto on_initialize() -> bool override {
        vent::log()->info("client", "minimal_client starting!");

        // test asset system
        vent::asset()->mount("app://", ".");
        vent::shader_asset* shader =
            vent::asset()->load_shader("app://assets/shaders/shader.slang.spv");
        if (shader) {
            vent::log()->info("client",
                              "successfully loaded shader ({} bytes)!",
                              shader->spirv_bytecode.size() * 4);

            vent::pipeline_desc desc;
            desc.shader = shader;
            // The default entry points are "vertMain" and "fragMain", which
            // matches our Slang shader.
            _pipeline = vent::renderer()->create_graphics_pipeline(desc);
            if (_pipeline) {
                vent::log()->info("client",
                                  "successfully created graphics pipeline!");
            } else {
                vent::log()->error("client",
                                   "failed to create graphics pipeline!");
            }
        } else {
            vent::log()->error("client", "failed to load shader!");
        }

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
                vent::log()->error("client", "failed to create window {}.", i);
                return false;
            }

            window->show();
            _windows.push_back(window);
            vent::log()->info("client",
                              "created window {} of 3: '{}'.",
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
        if (_frame_count == 60) {
            vent::window_desc desc;
            desc.title    = std::string("vent - delayed window");
            desc.width    = 1920;
            desc.height   = 1080;
            desc.style    = vent::window_style::standard;
            desc.renderer = vent::renderer_api::vulkan;
            _windows.push_back(vent::platform()->create_window(desc));
            _windows.back()->show();
        }

        // render to all windows
        for (auto* window : _windows) {
            if (vent::renderer()->begin_frame(window)) {
                // ... render commands would go here ...
                vent::renderer()->end_frame(window);
            }
        }

        if (_frame_count == 180) {
            request_exit();
        }
    }

    auto on_shutdown() -> void override {
        vent::log()->trace("client", "minimal_client::on_shutdown() called");
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
    std::vector<vent::ic_window*>      _windows;
    std::unique_ptr<vent::ic_pipeline> _pipeline;
    vent::u64                          _frame_count = 0;
    vent::f64                          _elapsed     = 0.0;
};

VENT_REGISTER_CLIENT(minimal_client);