// core module.
// vent entry point.
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

// --- globals ---
// —————————————————————————————————————————————————————————————————————————————

bool g_no_async = false;  // set by command line option --no_async.

/// @brief parse command line arguments and apply settings.
/// @note must be called before any system initialization.
/// @param argc command line argument count.
/// @param argv command line argument values.
auto parse_command_line(int argc, char** argv) -> void {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        // display backend: --x11 or --wayland.
        if (arg == "--x11" || arg == "-x11") {
            log()->trace("engine", "forcing X11 backend via command line");
#ifdef VENT_WINDOWS
            _putenv_s("VENT_DISPLAY_BACKEND", "x11");
#else
            setenv("VENT_DISPLAY_BACKEND", "x11", 1);
#endif
        } else if (arg == "--wayland" || arg == "-wayland") {
            log()->trace("engine", "forcing Wayland backend via command line");
#ifdef VENT_WINDOWS
            _putenv_s("VENT_DISPLAY_BACKEND", "wayland");
#else
            setenv("VENT_DISPLAY_BACKEND", "wayland", 1);
#endif
        } else if (arg == "--help" || arg == "-h") {
            log()->info("engine", "vent engine options:");
            log()->info("engine",
                        "  --x11, -x11          force X11/XWayland backend");
            log()->info("engine",
                        "  --wayland, -wayland  force Wayland backend");
            log()->info("engine", "  --help, -h           show this help");
        } else if (arg == "--no_async" || arg == "-no_async") {
            log()->trace("engine",
                         "disabling async job system via command line");
            g_no_async = true;
        }
    }
}

VENT_EXTERN_C VENT_API auto vent_engine_entry(const engine_config& config)
    -> int {
    // register this one as main thread in the registry.
    thread_registry::register_thread("MAIN");

    log()->trace("core", "entering vent_engine_entry. MAIN thread registered.");
    log()->info("core", "initializing vent engine for '{}'...", config.app_id);

    log()->trace(
        "core", "parsing command line arguments ({} args)...", config.argc);
    parse_command_line(config.argc, config.argv);

    log()->trace("core", "creating global system registry...");
    // create and set up the system registry.
    system_registry registry;
    set_system_registry(&registry);
    log()->trace("core", "global system registry set.");

    log()->trace("core", "passing control to registry.initialize_all()...");
    // initialize all systems (bootstrap first, load plugins, then regular).
    if (!registry.initialize_all(config)) {
        log()->critical("core", "failed to initialize engine systems");
        thread_registry::unregister_thread();
        set_system_registry(nullptr);
        return 1;
    }

    log()->info("core", "vent engine initialized successfully");
    log()->trace("core", "entering main loop...");

    // run main loop. returns when client signals exit or error.
    int exit_code = registry.run_main_loop();

    log()->trace("core",
                 "main loop exited with code {}. beginning shutdown...",
                 exit_code);

    // shutdown all systems in reverse order.
    registry.shutdown_all();

    log()->trace("core", "clearing global system registry pointer...");
    // clear the global registry pointer.
    set_system_registry(nullptr);

    log()->info("core", "goodbye from '{}'...", config.app_id);

    log()->trace("core", "unregistering MAIN thread.");
    thread_registry::unregister_thread();

    return exit_code;
}

}  // namespace vent