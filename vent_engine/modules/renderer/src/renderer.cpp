// renderer module.
// renderer frontend system implementation.
// ——————————————————————
//
// details.

#include <renderer.hpp>

#include <core/interfaces/i_system_registry.hpp>
#include <core/system/registration.hpp>

#include <_vent/accessors.hpp>

namespace vent {

auto renderer_system::initialize_s0() -> initialization_result {

    // load the render backend using the system registry.
    // todo: for now, we only have one backend. once there are multiple, we
    // should todo: decide on a selection strategy. config, envar, runtime
    // config, ...

    log()->trace("renderer", "  init stage 1: loading render backend plugin...");
    auto& registry = static_cast<i_system_registry&>(system());
    if (!registry.load_plugin_library("vent_vulkan_backend")) {
        log()->error("renderer", "failed to load vulkan backend plugin.");
        return initialization_result::failed();
    }

    log()->trace("renderer", "  init stage 1: initializing render backend plugin...");
    if (!registry.initialize_plugin_systems("vent_vulkan_backend")) {
        log()->error("renderer", "failed to initialize vulkan backend plugin.");
        return initialization_result::failed();
    }

    _backend = system().get<i_render_backend>();

    log()->info(
        "renderer", "render backend loaded: {}", _backend->get_api_name());

    log()->trace("renderer", "  init stage 1: render backend initialized.");

    // for now, complete.
    return initialization_result::complete();

    // load the plugin using the registry.

    // initialize all systems from this plugin.
    // this will call on_initialization() for each system.

    // check if a window without renderer exists. if so, continue with
    // initialize_s1() using that window as the target. otherwise, wait for a
    // window.created event and use that as the target.
}

auto renderer_system::shutdown() -> void {
}

}  // namespace vent

VENT_REGISTER_SYSTEM(vent::renderer_system, vent::i_renderer);