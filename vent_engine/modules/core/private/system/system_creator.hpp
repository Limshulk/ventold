#pragma once
//
// core module.
// system_creator.
// ——————————————————————
//
// owns pending systems and provides system creation functionality.
// provides the global pending list and thread-safe system registration.
//
// note the differenciation: loading, creating, initializing.
// - loading: load plugin libraries from disk. modules are always loaded as they
//   are linked into the main libvent_engine.so. loading plugins is neither
//   handled nor called by the system_creator but by the system_registry.
// - creating: instantiating system objects from registered factories. takes a
//   few microseconds i guess.
// - initializing: calling the system's initialization function and running any
//   of it's potentially expensive startup code.

#include <_vent/vent_sdk.hpp>

#include <mutex>

namespace vent {

// forward declarations.
class system_registry;

struct pending_entry;

class system_creator final {
public:
    system_creator()  = default;
    ~system_creator() = default;

    VENT_NO_COPY_MOVE(system_creator);

private:
    /// @brief mutex to protect access to the pending system list.
    std::mutex _pending_mutex;

    /// @brief holds a list of all systems that have been registered but not yet created.
    std::vector<pending_entry> _pending_systems;

public:
    /// @brief create system instances from all pending registrations.
    /// @param registry the system registry to add created systems to.
    /// @return number of systems created from pending registrations.
    auto create_from_pending(system_registry& registry) -> u32;

};

// --- global registration functions ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief get the global system_creator instance.
/// @return reference to the global system_creator instance.
auto get_system_creator() -> system_creator&;

}  // namespace vent
