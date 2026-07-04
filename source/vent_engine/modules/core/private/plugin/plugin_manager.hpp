#pragma once
//
// core module.
// plugin manager.
// ——————————————————————
//
// manages shared library loading and unloading for plugins. does not handle
// system creation or initialization by plugins, it really just cares about the
// file i/o.
// all operations are protected by an internal mutex and can be called from any
// thread.

#include <_vent/vent_sdk.hpp>
#include <core/utils/library.hpp>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vent {

/// @brief manages shared library loading/unloading for plugins.
/// thread-safe. handles only library i/o, not system creation.
class plugin_manager {
public:
    plugin_manager() = default;

    /// @brief destructor unloads all plugins.
    ~plugin_manager();

    VENT_NO_COPY_MOVE(plugin_manager);

    // --- loading / unloading ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief load a plugin library by name.
    /// @param name plugin name (without lib prefix or .so/.dll extension).
    /// @param path_prefix directory prefix for the library path (default:
    /// "./").
    /// @return true if loaded successfully or already loaded.
    auto load(std::string_view name, std::string_view path_prefix = "./")
        -> bool;

    /// @brief unload a plugin library by name.
    /// @param name plugin name.
    /// @return true if unloaded, false if wasn't loaded.
    auto unload(std::string_view name) -> bool;

    /// @brief unload all loaded plugins.
    auto unload_all() -> void;

    // --- queries ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief check if a plugin is currently loaded.
    /// @param name plugin name.
    /// @return true if the plugin is loaded.
    [[nodiscard]]
    auto is_loaded(std::string_view name) const -> bool;

    /// @brief get the library handle for a loaded plugin.
    /// @param name plugin name.
    /// @return library handle if loaded, INVALID_LIBRARY_HANDLE otherwise.
    [[nodiscard]]
    auto get_handle(std::string_view name) const -> lib::library_handle;

    /// @brief get the number of loaded plugins.
    /// @return count of currently loaded plugins.
    [[nodiscard]]
    auto count() const -> u32;

private:
    /// @brief loaded plugin handles. maps plugin_name -> library_handle.
    std::unordered_map<std::string, lib::library_handle> _plugins;

    /// @brief mutex for thread-safe access to _plugins.
    mutable std::mutex _mutex;
};

}  // namespace vent
