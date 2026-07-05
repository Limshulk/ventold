// minimal client application.
// ——————————————————————
//
// demonstrates a basic vent client using the client_base class.

#include <_vent/_vent.hpp>

#include <_vent/asset/shader.hpp>

#include <_vent/math/math.hpp>

class minimal_client : public vent::client_base {
public:
    minimal_client() = default;

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
        }

        vent::asset()->mount("app://", ".");

        // create the scene entity
        _model_entity = vent::world()->create_entity();
        vent::world()->set_mesh(
            _model_entity,
            vent::mesh_component {.model_path = "app://assets/viking_room.obj",
                                  .texture_path =
                                      "app://assets/viking_room.png"});

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

        // update entity transform
        vent::math::mat4 model =
            vent::math::rotate_z(_elapsed * vent::math::radians(90.0f));
        vent::world()->set_transform(
            _model_entity, vent::transform_component {.matrix = model});

        // setup camera
        vent::math::mat4 view =
            vent::math::look_at(vent::math::vec3(2.0f, 2.0f, 2.0f),
                                vent::math::vec3(0.0f, 0.0f, 0.0f),
                                vent::math::vec3(0.0f, 0.0f, 1.0f));

        vent::math::mat4 proj = vent::math::perspective(
            vent::math::radians(45.0f), 1280.0f / 720.0f, 0.1f, 10.0f);

        vent::renderer()->set_camera(view, proj);

        // render to all windows
        for (auto* window : _windows) {
            if (vent::renderer()->begin_frame(window)) {

                vent::renderer()->end_frame(window);
            }
        }

        if (_frame_count == 180) {
            request_exit();
        }
    }

    auto on_shutdown() -> void override {
        vent::log()->trace("client", "minimal_client::on_shutdown() called");

        if (_model_entity != vent::INVALID_ENTITY) {
            vent::world()->destroy_entity(_model_entity);
        }

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
    vent::entity                  _model_entity = vent::INVALID_ENTITY;
    vent::u64                     _frame_count  = 0;
    vent::f64                     _elapsed      = 0.0;
};

VENT_REGISTER_CLIENT(minimal_client);