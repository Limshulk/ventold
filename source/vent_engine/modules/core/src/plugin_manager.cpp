// core module.
// plugin manager implementation.
// ——————————————————————
//
// implements the plugin_manager class for shared library loading.

#include <plugin/plugin_manager.hpp>

#include <_vent/accessors.hpp>

#include <thread>

namespace vent {

plugin_manager::~plugin_manager() {
    unload_all();
}

auto plugin_manager::load(std::string_view name, std::string_view path_prefix)
    -> bool {
    std::lock_guard lock(_mutex);

    // check if already loaded.
    std::string name_str(name);
    log()->trace("plugin_manager", "checking if plugin '{}' is already loaded.", name);
    if (_plugins.contains(name_str)) {
        log()->trace("plugin_manager", "plugin '{}' is already loaded, skipping.", name);
        return true;  // it's in memory already.
    }

    // build library path.
    std::string library_path =
        std::string(path_prefix) + lib::make_shared_library_name(name.data());

    log()->trace("plugin_manager", "loading plugin: {}", library_path);

    // load the library.
    auto handle = lib::load_library(library_path.c_str());
    if (handle == lib::INVALID_LIBRARY_HANDLE) {
        log()->error("plugin_manager",
                     "failed to load plugin '{}': {}.",
                     name,
                     lib::get_last_error());
        return false;
    }

    // store the handle.
    _plugins.emplace(name_str, handle);

    log()->trace("plugin_manager", "plugin '{}' loaded successfully.", name);
    return true;
}

auto plugin_manager::unload(std::string_view name) -> bool {
    std::lock_guard lock(_mutex);

    log()->trace("plugin_manager", "attempting to unload plugin '{}'", name);
    auto it = _plugins.find(std::string(name));
    if (it == _plugins.end()) {
        log()->debug("plugin_manager", "plugin '{}' not found, cannot unload.", name);
        return false;
    }

    log()->trace("plugin_manager", "unloading plugin: {}", name);

    lib::unload_library(it->second);
    _plugins.erase(it);

    log()->trace("plugin_manager", "plugin '{}' unloaded successfully.", name);

    return true;
}

auto plugin_manager::unload_all() -> void {
    std::lock_guard lock(_mutex);
    
    log()->trace("plugin_manager", "unloading all plugins...");

    for (auto& [name, handle] : _plugins) {
        log()->trace("plugin_manager", "unloading plugin: {}", name);
        lib::unload_library(handle);
    }

    _plugins.clear();
    log()->trace("plugin_manager", "all plugins unloaded.");
}

auto plugin_manager::is_loaded(std::string_view name) const -> bool {
    std::lock_guard lock(_mutex);
    return _plugins.contains(std::string(name));
}

auto plugin_manager::get_handle(std::string_view name) const
    -> lib::library_handle {
    std::lock_guard lock(_mutex);

    auto it = _plugins.find(std::string(name));
    if (it == _plugins.end()) {
        return lib::INVALID_LIBRARY_HANDLE;
    }

    return it->second;
}

auto plugin_manager::count() const -> u32 {
    std::lock_guard lock(_mutex);
    return static_cast<u32>(_plugins.size());
}

}  // namespace vent
