#pragma once
//
// core module.
// engine-faced system_registry interface.
// ——————————————————————
//
// extends the client-faced ic_system_registry with additional functionality and
// lifecycle management for engine-internal use.

#include <_vent/core/ic_system_registry.hpp>

#include <string_view>

namespace vent {

class i_system_registry : public ic_system_registry {
public:
    // --- dynamic plugin library management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief load a plugin library (blocking) without initializing its
    /// systems. loads the shared library and runs static initializers. systems
    /// are NOT created or initialized yet.
    /// @param plugin_name plugin base name without lib prefix/extension.
    /// ("vent_vulkan_backend" for "libvent_vulkan_backend.so").
    /// @return true if library loaded successfully.
    virtual auto load_plugin_library(std::string_view plugin_name) -> bool = 0;

    /// @brief unload a plugin library. does NOT shutdown systems - caller must
    /// call shutdown_plugin_systems() first if systems were initialized.
    /// @param plugin_name the plugin base name without lib prefix/extension.
    virtual auto unload_plugin_library(std::string_view plugin_name) -> void = 0;

    /// @brief check if a plugin library is loaded.
    /// @param plugin_name the plugin base name.
    /// @return true if the plugin library is loaded.
    [[nodiscard]]
    virtual auto is_plugin_loaded(std::string_view plugin_name) const
        -> bool = 0;

    // --- dynamic system initialization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize all pending systems from a specific plugin (async
    /// system initialization, but blocking until all are done). creates system
    /// instances from pending registrations for this plugin, then initializes
    /// them (respecting dependencies).
    /// @param plugin_name the plugin whose systems should be initialized.
    /// @return true if all systems initialized successfully.
    virtual auto initialize_plugin_systems(std::string_view plugin_name)
        -> bool = 0;

    /// @brief shutdown all systems from a specific plugin. shuts down systems
    /// in reverse initialization order.
    /// @param plugin_name the plugin whose systems should be shut down.
    virtual auto shutdown_plugin_systems(std::string_view plugin_name)
        -> void = 0;
};

}  // namespace vent