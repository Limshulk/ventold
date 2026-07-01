// core module.
// main entry point.
// ——————————————————————
//
// implements the main entry point called by the launcher executable.
//
// the core engine module is stripped down to the bare minimum intentionally. it
// provides only absolutely necessary functionality to load other modules and
// initialize their systems.

#include <_vent/_vent.hpp>

#include <core/thread_registry.hpp>

#include <system/system_registry.hpp>

namespace vent {

/// @brief parse command line arguments and apply settings.
/// @note must be called before any system initialization.
/// @param argc command line argument count.
/// @param argv command line argument values.
auto parse_command_line(int argc, char** argv) -> void {
    // nothing to do here yet.
}

VENT_EXTERN_C VENT_API auto vent_engine_entry(const engine_config& config)
    -> int {
    thread_registry::register_thread("MAIN");

    log()->info("core", "initializing vent engine for '{}'...", config.app_id);

    parse_command_line(config.argc, config.argv);

    system_registry registry;
    if(!registry.initialize_all(config)) {
        log()->error("core", "failed to initialize engine systems. exiting.");
        thread_registry::unregister_thread();
        return 1;
    }

    registry.shutdown_all();
    thread_registry::unregister_thread();

    return 0;
}

}  // namespace vent