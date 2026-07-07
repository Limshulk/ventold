// minimal client application.
// ——————————————————————
//
// demonstrates a basic vent client using the client_base class.
// the renderer runs automatically as an ir_runnable — clients describe
// the scene via world components (mesh, transform, camera) and never
// issue render API calls directly.

#include <_vent/_vent.hpp>

#include <_vent/event_bus/event_coroutine.hpp>
#include <_vent/math/math.hpp>

namespace {

/// @brief demonstrates awaiting an event from a coroutine. runs eagerly, logs,
/// then suspends at the co_await until "demo.milestone" is published — resuming
/// on the main thread (the default delivery) at the frame-start drain. this is
/// the "run, then sync on a fired event" pattern, without blocking a thread.
auto await_demo_event() -> vent::co_task {
    vent::log()->info("client", "coroutine: awaiting 'demo.milestone'...");
    co_await vent::await_event("demo.milestone");
    vent::log()->info("client",
                      "coroutine: 'demo.milestone' fired — resumed on main.");
}

}  // namespace

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
            vent::ic_asset::system_name,
            vent::ic_world::system_name};
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

        // note: no asset mount here. the engine mounts app:// to the
        // executable's directory by default, so app://assets/... resolves
        // correctly no matter which working directory launched us.

        // create the scene entity (the rotating viking room model).
        _model_entity = vent::world()->create_entity();
        vent::world()->set_mesh(
            _model_entity,
            vent::mesh_component {.model_path = "app://assets/viking_room.obj",
                                  .texture_path =
                                      "app://assets/viking_room.png"});

        // create a camera entity. the renderer frontend runs as ir_runnable
        // at run_phase_render — it reconciles surfaces, extracts the world
        // once per frame, computes view/proj from the camera entity, and
        // replays per window. no manual begin_frame/end_frame/set_camera.
        _camera_entity = vent::world()->create_entity();
        vent::world()->set_camera(
            _camera_entity,
            vent::camera_component {.fov_y_deg = 45.0f,
                                    .z_near    = 0.1f,
                                    .z_far     = 100.0f});
        // careful: a transform component stores a POSE (camera-to-world), so
        // we use look_at_transform() here — look_at() returns the opposite
        // direction (a view matrix), which the renderer derives itself by
        // inverting this pose.
        vent::world()->set_transform(
            _camera_entity,
            vent::transform_component {
                .matrix = vent::math::look_at_transform(
                    vent::math::vec3(2.0f, 2.0f, 2.0f),
                    vent::math::vec3(0.0f, 0.0f, 0.0f),
                    vent::math::vec3(0.0f, 0.0f, 1.0f))});
        // set as default camera so all windows use it.
        vent::world()->set_active_camera(_camera_entity);

        // launch a coroutine that awaits an event (fired at frame 60 below).
        // it runs now, logs, and suspends until "demo.milestone" is published.
        await_demo_event();

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
            // create_window can fail (returns nullptr). only track & show it if
            // it actually succeeded, matching the null check in on_initialize.
            if (auto* delayed = vent::platform()->create_window(desc)) {
                _windows.push_back(delayed);
                delayed->show();
            } else {
                vent::log()->error("client",
                                   "failed to create delayed window.");
            }

            // fire the event the demo coroutine is awaiting. it was subscribed
            // with main delivery, so the coroutine resumes at the next frame's
            // main-thread drain — not here on this publish.
            vent::event()->publish("demo.milestone");
        }

        // update entity transform (spinning the model around Z).
        vent::math::mat4 model =
            vent::math::rotate_z(_elapsed * vent::math::radians(90.0f));
        vent::world()->set_transform(
            _model_entity, vent::transform_component {.matrix = model});

        // note: rendering is fully automatic. the renderer frontend runs as
        // ir_runnable in the render phase — it reconciles surfaces, extracts
        // the world into a command list once per frame, derives view/proj
        // from the camera entity and the window framebuffer size, and replays
        // per window. the client only describes the scene through world
        // components (mesh, transform, camera) and never issues render calls.

        if (_frame_count == 180) {
            request_exit();
        }
    }

    auto on_shutdown() -> void override {
        vent::log()->trace("client", "minimal_client::on_shutdown() called");

        if (_model_entity != vent::INVALID_ENTITY) {
            vent::world()->destroy_entity(_model_entity);
        }
        if (_camera_entity != vent::INVALID_ENTITY) {
            vent::world()->destroy_entity(_camera_entity);
        }

        for (auto it = _windows.rbegin(); it != _windows.rend(); ++it) {
            vent::platform()->destroy_window(*it);
        }
        _windows.clear();

        // guard the average against an exit before the first full frame.
        const vent::f64 avg_fps =
            _elapsed > 0.0 ? _frame_count / _elapsed : 0.0;
        vent::log()->info("client",
                          "goodbye! total frames: {}, avg fps: {:.1f}",
                          _frame_count,
                          avg_fps);
    }

    [[nodiscard]]
    auto close_policy() const -> vent::window_close_policy override {
        return vent::window_close_policy::exit_on_main_close;
    }

private:
    std::vector<vent::ic_window*> _windows;
    vent::entity                  _model_entity  = vent::INVALID_ENTITY;
    vent::entity                  _camera_entity = vent::INVALID_ENTITY;
    vent::u64                     _frame_count   = 0;
    vent::f64                     _elapsed       = 0.0;
};

VENT_REGISTER_CLIENT(minimal_client);
