// core module.
// system_registry implementation.
// ——————————————————————
//
// details.

#include <system/system_registry.hpp>
#include <system/system_creator.hpp>

#include <_vent/accessors.hpp>

namespace vent {

system_registry::~system_registry() {
    if(_initialized)
        shutdown_all();

}

auto system_registry::initialize_all(const engine_config& config) -> bool {
    if(_initialized) {
        log()->warn_f("system_registry", "already initialized.");
        return true;
    }

    auto& _creator = get_system_creator();

    _initialized = true;

    return true;
}

auto system_registry::shutdown_all() -> void {
    _initialized = false;
}

}