// core module.
// plugin manager implementation.
// ——————————————————————
//
// implements the plugin_manager class for shared library loading.

#include <plugin/plugin_manager.hpp>

#include <_vent/accessors.hpp>

namespace vent {

plugin_manager::~plugin_manager() {
    unload_all();
}

auto plugin_manager::load(std::string_view name, 
                          std::string_view path_prefix) -> bool {
    std::lock_guard lock(_mutex);

    // check if already loaded.
    std::string name_str(name);
    if (_plugins.contains(name_str)) {
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
                     "failed to load plugin '{}': {}",
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

    auto it = _plugins.find(std::string(name));
    if (it == _plugins.end()) {
        return false;
    }

    log()->trace("plugin_manager", "unloading plugin: {}", name);

    lib::unload_library(it->second);
    _plugins.erase(it);

    return true;
}

auto plugin_manager::unload_all() -> void {
    std::lock_guard lock(_mutex);

    for (auto& [name, handle] : _plugins) {
        log()->trace("plugin_manager", "unloading plugin: {}", name);
        lib::unload_library(handle);
    }

    _plugins.clear();
}

auto plugin_manager::is_loaded(std::string_view name) const -> bool {
    std::lock_guard lock(_mutex);
    return _plugins.contains(std::string(name));
}

auto plugin_manager::get_handle(std::string_view name) const -> lib::library_handle {
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
