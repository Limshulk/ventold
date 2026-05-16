// core module.
// global accessor implementations.
// ——————————————————————

#include <_vent/accessors.hpp>

#include <fallback/fallback_log.hpp>

namespace vent {

// --- exported implementations ---
// ——————————————————————————————————————————————————————————————————————————————

VENT_API auto log() -> ic_log* {
    // try real log system first, fall back to printf-based logging.
    /*if (g_system_registry) [[likely]] {
        if (auto* l = g_system_registry->get_if_ready<ic_log>()) [[likely]]
        {
            return l;
        }
    }*/
    return &fallback_log::instance();
}

}  // namespace vent