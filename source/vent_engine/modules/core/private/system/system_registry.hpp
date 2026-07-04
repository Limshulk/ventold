#pragma once
//
// core module.
// system_registry implementation header.
// ——————————————————————
//
// concrete implementation of the system_registry. manages all registered
// systems, provides lookups, and caches role interfaces for the main loop.
// initialization orchestration is handled by system_creator.
// plugin library i/o is handled by plugin_manager.

#include <core/interfaces/i_system_registry.hpp>
#include <core/interfaces/i_event_bus.hpp>

#include <_vent/core/ir_runnable.hpp>
#include <_vent/core/ir_client.hpp>

#include <system/system_entry.hpp>
#include <system/main_loop.hpp>
#include <plugin/plugin_manager.hpp>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vent {

class system_registry final : public i_system_registry {
public:
    system_registry() = default;
    ~system_registry() override;

    VENT_NO_COPY_MOVE(system_registry);

private:
    // --- storage ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief true if initialize_all() has completed successfully.
    bool _initialized = false;

    /// @brief map of all systems. name -> system_entry.
    // todo: does this ened a mutex?
    std::unordered_map<std::string, system_entry> _systems;

    /// @brief map: type_index -> (void* interface_ptr, system_name).
    std::unordered_map<std::type_index, std::pair<void*, std::string>>
        _interfaces;

    /// @brief initialization order (for reverse-order shutdown).
    std::vector<std::string> _init_order;

    /// @brief mutex for _init_order (concurrent writes from job threads).
    mutable std::mutex _init_order_mutex;

    // --- cached role interfaces ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief cached event bus pointer (set after bootstrap init).
    ic_event_bus* _event = nullptr;

    /// @brief cached client interface controlling main loop lifecycle.
    ir_client* _client = nullptr;

    /// @brief cached list of runnable systems for main loop.
    std::vector<ir_runnable*> _runnables;

    /// @brief main loop handler.
    main_loop _main_loop;

    // --- plugin tracking ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief plugin library manager.
    plugin_manager _plugin_manager;

    /// @brief tracks which plugin each system came from. maps system_name ->
    /// plugin_name. empty string for systems not from plugins.
    std::unordered_map<std::string, std::string> _system_to_plugin;

    /// @brief mutex for _system_to_plugin.
    mutable std::mutex _plugin_tracking_mutex;

protected:
    // --- ic_system_registry ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto get_interface_ptr(std::type_info const& interface_type)
        -> void* override;
    [[nodiscard]]
    auto get_interface_ptr_if_ready(std::type_info const& interface_type)
        -> void* override;
    [[nodiscard]]
    auto has_role_impl(std::string_view      system_name,
                       std::type_info const& role_type) -> bool override;
    [[nodiscard]]
    auto get_role_impl(std::string_view      system_name,
                       std::type_info const& role_type) -> void* override;

public:
    // --- i_system_registry ---
    // —————————————————————————————————————————————————————————————————————————

    auto load_plugin_library(std::string_view plugin_name) -> bool override;
    auto unload_plugin_library(std::string_view plugin_name) -> void override;
    [[nodiscard]]
    auto is_plugin_loaded(std::string_view plugin_name) const -> bool override;

    auto initialize_plugin_systems(std::string_view plugin_name)
        -> bool override;
    auto shutdown_plugin_systems(std::string_view plugin_name) -> void override;

    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize all systems. creates from pending, bootstraps
    /// sequentially, loads plugins, then initializes regular systems in
    /// parallel via system_creator.
    /// @param config engine configuration from launcher.
    /// @return true if all systems initialized successfully.
    auto initialize_all(const engine_config& config) -> bool;

    /// @brief run the main loop. delegates to main_loop.
    /// @return exit code (0 = success, non-zero = error).
    auto run_main_loop() -> int;

    /// @brief shutdown all systems in reverse init order, then unload plugins.
    auto shutdown_all() -> void;

    // --- system management (used by system_creator) ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief add a system to the registry.
    /// @param name unique name of the system.
    /// @param entry system entry containing instance and interfaces.
    /// @param source_plugin plugin name (empty for module systems).
    /// @return true if added successfully.
    auto add_system(const std::string& name,
                    system_entry&&     entry,
                    const std::string& source_plugin) -> bool;

    /// @brief get a mutable system entry pointer.
    /// @param name name of the system.
    /// @return pointer to the entry, or nullptr if not found.
    [[nodiscard]]
    auto get_entry(const std::string& name) -> system_entry*;

    /// @brief get all system names in the registry.
    /// @return vector of system names.
    [[nodiscard]]
    auto get_all_system_names() const -> std::vector<std::string>;

    /// @brief check if a system is ready (alias for is_system_ready).
    /// @param name name of the system.
    /// @return true if fully initialized.
    [[nodiscard]]
    auto is_ready(const std::string& name) const -> bool;

    /// @brief mark a system as ready. adds to init order and publishes ready
    /// event.
    /// @param name name of the system.
    auto mark_system_ready(const std::string& name) -> void;

    /// @brief mark a system as failed. publishes failure event.
    /// @param name name of the system.
    auto mark_system_failed(const std::string& name) -> void;

private:
    // --- internal ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief check if a system is fully initialized.
    /// @param name name of the system.
    /// @return true if found and ready.
    [[nodiscard]]
    auto is_system_ready(std::string_view name) const -> bool;

    /// @brief rebuild cached role interfaces (ir_runnable, ir_client) from
    /// current system state and configure the main loop.
    auto cache_role_interfaces() -> void;
};

// --- internal accessor ---
// ——————————————————————————————————————————————————————————————————————————————

/// @brief set the global registry pointer for accessors.
/// @param registry pointer to the system registry instance.
auto set_system_registry(ic_system_registry* registry) -> void;

}  // namespace vent