// core module.
// global accessor implementations.
// ——————————————————————

#include <_vent/accessors.hpp>

#include <fallback/fallback_job.hpp>
#include <fallback/fallback_log.hpp>

namespace vent {

// defined in system_registry.cpp.
extern ic_system_registry* g_system_registry;

extern bool g_no_async;  // set by command line option --no_async. defined in
                         // vent_engine_entry.cpp.

// --- exported implementations ---
// ——————————————————————————————————————————————————————————————————————————————

VENT_API auto system() -> ic_system_registry& {
    return *g_system_registry;
}

VENT_API auto asset() -> ic_asset* {
    return g_system_registry->get<ic_asset>();
}

VENT_API auto job() -> ic_job* {
    // try real job system first, fall back to inline execution.
#if defined(VENT_DEBUG)
    if (g_no_async)
        return &fallback_job::instance();
#endif

    if (g_system_registry) [[likely]] {
        if (auto* j = g_system_registry->get_if_ready<ic_job>()) [[likely]]
        {
            return j;
        }
    }
    return &fallback_job::instance();
}

VENT_API auto log() -> ic_log* {
// try real log system first, fall back to printf-based logging.

// in debug builds, we always use the fallback log for sync output.
#ifdef VENT_DEBUG
    return &fallback_log::instance();
#endif
    if (g_system_registry) [[likely]] {
        if (auto* l = g_system_registry->get_if_ready<ic_log>()) [[likely]]
        {
            return l;
        }
    }
    return &fallback_log::instance();
}

VENT_API auto event() -> ic_event_bus* {
    return g_system_registry->get<ic_event_bus>();
}

VENT_API auto platform() -> ic_platform* {
    return g_system_registry->get<ic_platform>();
}

VENT_API auto renderer() -> ic_renderer* {
    return g_system_registry->get<ic_renderer>();
}

VENT_API auto world() -> ic_world* {
    return g_system_registry->get<ic_world>();
}

// --- return if ready accessors ---
// —————————————————————————————————————————————————————————————————————————————

VENT_API auto event_if_ready() -> ic_event_bus* {
    return g_system_registry ? g_system_registry->get_if_ready<ic_event_bus>()
                             : nullptr;
}

VENT_API auto platform_if_ready() -> ic_platform* {
    return g_system_registry ? g_system_registry->get_if_ready<ic_platform>()
                             : nullptr;
}

// --- log helper implementations ---
// —————————————————————————————————————————————————————————————————————————————

void ic_log::format_and_log(log_level        lvl,
                            const char*      module,
                            std::string_view fmt,
                            std::format_args args) {
    std::string result = std::vformat(fmt, args);
    this->message(lvl, module, result.c_str());
}

void ic_log::format_and_log_full(log_level                   lvl,
                                 const char*                 module,
                                 const std::source_location& loc,
                                 std::string_view            fmt,
                                 std::format_args            args) {
    std::string result = std::vformat(fmt, args);
    this->message_full(lvl,
                       module,
                       result.c_str(),
                       loc.file_name(),
                       loc.function_name(),
                       static_cast<u32>(loc.line()));
}

}  // namespace vent
