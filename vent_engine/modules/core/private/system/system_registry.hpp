#pragma once
//
// core module.
// system_registry implementation header.
// ——————————————————————
//
// concrete implementation of the system_registry.
// the system_registry manages all registered systems, provides lookups and
// caches role interfaces. initialization is handled by system_creator. plugins
// are handled by plugin_manager.

#include <_vent/vent_sdk.hpp>

#include <core/interfaces/i_system_registry.hpp>

namespace vent {

class system_registry final : public i_system_registry {
public:
    system_registry() = default;
    ~system_registry() override;

    VENT_NO_COPY_MOVE(system_registry);

private:
    // --- storage ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set to true after initialize_all() has completed successfully.
    bool _initialized = false;

public:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize all systems. creates all systems from pending,
    /// initializes bootstrap systems sequentially, loads all plugins (in
    /// parallel, if available), then initializes all regular systems (in
    /// parallel, if available) via system_creator.
    /// @param config engine configuration obtained from the launcher.
    /// @return true, if all systems initialized successfully.
    [[nodiscard]]
    auto initialize_all(const engine_config& config) -> bool;

    /// @brief shuts down all systems in reverse init order, then unload plugins.
    /// @return
    auto shutdown_all() -> void;
};

}  // namespace vent