// core module.
// system_creator implementation.
// ——————————————————————
//
// details.

#include <system/system_creator.hpp>

#include <_vent/accessors.hpp>

namespace vent {

/// @brief system_creator is a singleton. this function owns the global instance.
/// @return reference to the global system_creator instance.
static system_creator& get_creator_instance() {
    static system_creator instance;
    return instance;
}

/// @brief get the global system_creator instance.
/// @return reference to the global system_creator instance.
auto get_system_creator() -> system_creator& {
    return get_creator_instance();
}

auto system_creator::create_from_pending(system_registry& registry) -> u32 {
    std::lock_guard lock(_pending_mutex);

    u32 count = 0;
    for (auto& entry : _pending_systems) {
        // create system instance.
        auto sys  = entry.factory();
        auto name = std::string(sys->name());

        // create system_entry and populate interface map.
        system_entry se;
        se.instance = std::move(sys);
        entry.map_interfaces(se.instance.get(), se.interfaces);

        // set pending dependencies if system implements ir_dependencies role.
        if (auto* deps = dynamic_cast<ir_dependencies*>(se.instance.get())) {
            auto dep_span = deps->get_dependencies();
            se.pending_dependencies = std::vector<std::string>(dep_span.begin(), dep_span.end());
        }

        // add to registry.
        if (registry.add_system(name, std::move(se), entry.source_plugin))
            count++;
    }

    log()->trace("system_creator", "{} systems created from pending registrations.", count);

    _pending_systems.clear();
    return count;
}

}  // namespace vent