// minimal client application.
// ——————————————————————
//
// demonstrates a basic vent client using the client_base class.

#include <_vent/_vent.hpp>

#include <_vent/asset/shader.hpp>


#include <_vent/math/math.hpp>

#include <memory>

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
            vent::log()->info("client",
                              "created window {} of 3: '{}'.",
                              _windows.size(),
                              window->get_title());
        }

        // test math library
        vent::math::mat4 view = vent::math::look_at(
            vent::math::vec3(0.0f, -10.0f, 0.0f),  // eye (looking from -Y)
            vent::math::vec3(0.0f, 0.0f, 0.0f),    // center
            vent::math::vec3(0.0f, 0.0f, 1.0f)     // up (Z-up)
        );
        vent::math::mat4 proj = vent::math::perspective(
            vent::math::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        vent::log()->trace(
            "client", "math library test: view and proj matrices generated!");

        vent::log()->trace("client",
                           "multi-window test initialized with {} windows",
                           _windows.size());

        // test asset system.
        vent::asset()->mount("app://", ".");
        vent::shader_asset* shader =
            vent::asset()->load_shader("app://assets/shaders/shader.slang.spv");
        if (shader) {
            vent::log()->trace("client",
                               "successfully loaded shader ({} bytes)!",
                               shader->spirv_bytecode.size() *
                                   sizeof(vent::u32));

            vent::pipeline_desc desc;
            desc.shader         = shader;
            desc.vertex_entry   = "vertMain";
            desc.fragment_entry = "fragMain";
            _pipeline   = vent::renderer()->create_graphics_pipeline(desc);
            if (_pipeline != vent::INVALID_PIPELINE_HANDLE) {
                vent::log()->trace("client",
                                   "successfully created graphics pipeline!");
            } else {
                vent::log()->error("client",
                                   "failed to create graphics pipeline!");
            }
        } else {
            vent::log()->error("client", "failed to load shader!");
        }

        vent::vertex vertices[] = {
            {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.0f},  // red (top left)
            {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f},  // green (top right)
            {{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f},  // blue (bottom right)
            {{-0.5f,  0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0.0f, 1.0f}   // white (bottom left)
        };

        vent::u32 indices[] = {0, 1, 2, 2, 3, 0};

        _triangle_mesh = vent::renderer()->create_mesh(vertices, indices);

        // load texture
        vent::image_asset* image = vent::asset()->load_image("app://assets/texture.jpg");
        if (image) {
            vent::texture_desc tex_desc {
                .width = image->width,
                .height = image->height,
                .pixels = std::span<const vent::u8>(image->pixels)
            };
            _texture = vent::renderer()->create_texture(tex_desc);
            vent::log()->trace("client", "successfully created texture!");
        } else {
            vent::log()->error("client", "failed to load texture!");
        }

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

        // compute matrices for the uniform buffer.
        vent::math::mat4 model = vent::math::rotate_z(
            _elapsed * vent::math::radians(90.0f)
        );

        vent::math::mat4 view = vent::math::look_at(
            vent::math::vec3(2.0f, 2.0f, 2.0f),
            vent::math::vec3(0.0f, 0.0f, 0.0f),
            vent::math::vec3(0.0f, 0.0f, 1.0f)
        );

        vent::math::mat4 proj = vent::math::perspective(
            vent::math::radians(45.0f), 1280.0f / 720.0f, 0.1f, 10.0f
        );

        vent::uniform_buffer_object ubo = {
            .model = model,
            .view = view,
            .proj = proj
        };

        // render to all windows
        for (auto* window : _windows) {
            if (vent::renderer()->begin_frame(window)) {

                // update uniforms per frame (the active swapchain guarantees correct buffering).
                vent::renderer()->update_global_uniforms(ubo);

                if (_pipeline != vent::INVALID_PIPELINE_HANDLE && _triangle_mesh != vent::INVALID_MESH_HANDLE) {
                    vent::renderer()->bind_pipeline(_pipeline);

                    auto& cmd_list = vent::renderer()->get_command_list();
                    cmd_list.draw_mesh(_triangle_mesh, 0);

                    vent::command_list* lists[] = {&cmd_list};
                    vent::renderer()->submit_command_lists(lists);
                }
                vent::renderer()->end_frame(window);
            }
        }

        if (_frame_count == 180) {
            request_exit();
        }
    }

    auto on_shutdown() -> void override {
        vent::log()->trace("client", "minimal_client::on_shutdown() called");

        if (_texture != vent::INVALID_TEXTURE_HANDLE) {
            vent::renderer()->destroy_texture(_texture);
        }
        if (_triangle_mesh != vent::INVALID_MESH_HANDLE) {
            vent::renderer()->destroy_mesh(_triangle_mesh);
        }
        if (_pipeline != vent::INVALID_PIPELINE_HANDLE) {
            vent::renderer()->destroy_graphics_pipeline(_pipeline);
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
    std::vector<vent::ic_window*>      _windows;
    vent::pipeline_handle _pipeline = vent::INVALID_PIPELINE_HANDLE;
    vent::mesh_handle _triangle_mesh = vent::INVALID_MESH_HANDLE;
    vent::texture_handle _texture = vent::INVALID_TEXTURE_HANDLE;
    vent::u64         _frame_count   = 0;
    vent::f64         _elapsed       = 0.0;
};

VENT_REGISTER_CLIENT(minimal_client);