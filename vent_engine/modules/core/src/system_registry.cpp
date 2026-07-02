// core module.
// system_registry implementation.
// ——————————————————————
//
// implements the system_registry class. provides system storage, lookups,
// and role caching.

#include <system/system_registry.hpp>
#include <system/system_creator.hpp>

#include <core/interfaces/ir_bootstrap.hpp>

#include <_vent/accessors.hpp>
#include <_vent/interfaces/ir_dependencies.hpp>

namespace vent {

// --- internal accessor ---
// —————————————————————————————————————————————————————————————————————————————

// global registry pointer — used by accessors.cpp.
ic_system_registry* g_system_registry = nullptr;

auto set_system_registry(ic_system_registry* registry) -> void {
    g_system_registry = registry;
}

// --- lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

system_registry::~system_registry() {
    if (_initialized)
        shutdown_all();

    _systems.clear();
}

auto system_registry::initialize_all(const engine_config& config) -> bool {
    if (_initialized) {
        log()->warn_f("system_registry", "already initialized.");
        return true;
    }

    auto& creator = get_system_creator();

    // --- create module systems from static registrations ---
    // note: loading, creating, and initializing are separate steps.
    // - loading: plugin libraries from disk (modules are always linked).
    // - creating: instantiating system objects from registered factories.
    // - initializing: running potentially complex init logic per system.
    creator.create_from_pending(*this);

    // --- categorize systems into bootstrap and regular ---
    auto                     all_names = get_all_system_names();
    std::vector<std::string> bootstrap_systems;
    std::vector<std::string> regular_systems;

    creator.categorize_systems(
        *this, all_names, bootstrap_systems, regular_systems);

    log()->trace(
        "system_registry",
        "created {} bootstrap and {} regular systems from modules.",
        bootstrap_systems.size(),
        regular_systems.size());
    for (const auto& name : bootstrap_systems) {
        log()->trace("system_registry", "  - bs: {}.", name);
    }
    for (const auto& name : regular_systems) {
        log()->trace("system_registry", "  - rs: {}.", name);
    }

    // --- initialize bootstrap systems sequentially ---
    // bootstrap systems (event_bus, job_system, log, ...) are initialized
    // first on the main thread. they have no dependencies and no event
    // waiting. they provide the infrastructure for regular init.
    init_context bootstrap_ctx {
        .registry  = *this,
        .events    = nullptr,
        .jobs      = nullptr,
        .on_ready  = nullptr,
        .on_failed = nullptr,
    };

    if (!creator.initialize_bootstrap(bootstrap_ctx, bootstrap_systems)) {
        log()->error("system_registry",
                     "bootstrap system initialization failed.");
        return false;
    }

    log()->trace("system_registry",
                 "all {} bootstrap systems initialized successfully.",
                 bootstrap_systems.size());

    // --- set basic accessors ---
    // from here on, event() and job() are accessible. event() is core
    // functionality. job() is optional but has a fallback sequential forwarder.
    _event = get<ic_event_bus>();

    // --- load plugins ---
    if (config.plugin_count > 0) {
        log()->trace("system_registry",
                     "loading {} plugins...",
                     config.plugin_count);

        // load plugin libraries via job system for parallelism. each plugin's
        // static initializers register systems to the pending list.
        std::atomic<u32> load_failures {0};

        job()->parallel_for(
            0,
            config.plugin_count,
            [this, &config, &creator, &load_failures](u64 i) {
                std::string plugin_name = config.plugins[i];

                // set thread-local context so static initializers know which
                // plugin they belong to.
                creator.set_loading_context(plugin_name);

                bool ok = _plugin_manager.load(plugin_name);

                // clear context after load completes.
                creator.set_loading_context("");

                if (!ok) {
                    load_failures.fetch_add(1, std::memory_order_relaxed);
                }
            });

        if (load_failures.load(std::memory_order_relaxed) > 0) {
            log()->error("system_registry", "failed to load required plugins.");
            return false;
        }

        // create plugin systems from pending registrations.
        creator.create_from_pending(*this);

        // create the client (registered by plugin static initializers).
        if (!creator.create_client(*this)) {
            log()->error("system_registry", "no client registered by plugins.");
            return false;
        }

        // rebuild regular systems list with new plugin systems + client.
        auto prev_count = regular_systems.size();
        regular_systems.clear();
        for (const auto& [name, entry] : _systems) {
            if (!dynamic_cast<ir_bootstrap*>(entry.instance.get()) &&
                entry.state == system_state::pending)
                regular_systems.push_back(name);
        }

        log()->trace("system_registry",
                     "created {} additional regular systems from plugins.",
                     regular_systems.size() - prev_count);
    }

    // --- initialize regular systems in parallel ---
    log()->trace("system_registry",
                 "initializing {} regular systems...",
                 regular_systems.size());

    init_context regular_ctx {
        .registry  = *this,
        .events    = _event,
        .jobs      = job(),
        .on_ready  = nullptr,
        .on_failed = nullptr,
    };

    auto result = creator.initialize_regular(regular_ctx, regular_systems);

    if (!result.success()) {
        log()->error("system_registry",
                     "{} system(s) failed to initialize.",
                     result.failed);
        return false;
    }

    // cache role interfaces for main loop.
    cache_role_interfaces();

    _initialized = true;
    return true;
}

auto system_registry::run_main_loop() -> int {
    return _main_loop.run();
}

auto system_registry::shutdown_all() -> void {
    // shutdown fully initialized systems in reverse init order.
    for (auto it = _init_order.rbegin(); it != _init_order.rend(); ++it) {
        auto& entry = _systems[*it];
        if (entry.is_ready()) {
            if (_event) {
                _event->publish("system.shutdown." + *it);
            }
            log()->trace(
                "system_registry", "shutting down system '{}'...", *it);
            entry.instance->on_shutdown();
            entry.state = system_state::pending;
        }
    }

    // shutdown partially initialized systems (awaiting events but never
    // completed). unlikely in practice, but handles edge cases.
    for (auto& [name, entry] : _systems) {
        if (entry.state == system_state::awaiting_event) {
            for (auto& [event_name, sub_id] : entry.event_subscriptions) {
                if (_event) {
                    _event->unsubscribe(sub_id);
                }
            }
            entry.event_subscriptions.clear();
            entry.instance->on_shutdown();
        }
    }

    // clear role interface caches.
    _runnables.clear();
    _client = nullptr;

    // destroy all systems before unloading plugins. systems defined in plugins
    // must be destroyed before the plugin code is unloaded.
    _systems.clear();
    _interfaces.clear();

    log()->trace("system_registry", "all systems have been shut down.");

    // unload all plugins via plugin_manager.
    _plugin_manager.unload_all();
    log()->trace("system_registry", "all plugins have been unloaded.");

    _initialized = false;
}

// --- ic_system_registry ---
// —————————————————————————————————————————————————————————————————————————————

auto system_registry::get_interface_ptr(std::type_info const& interface_type)
    -> void* {
    auto it = _interfaces.find(std::type_index(interface_type));
    if (it == _interfaces.end())
        return nullptr;
    return it->second.first;
}

auto system_registry::get_interface_ptr_if_ready(
    std::type_info const& interface_type) -> void* {
    auto it = _interfaces.find(std::type_index(interface_type));
    if (it == _interfaces.end())
        return nullptr;

    const auto& sys_name = it->second.second;
    auto        sys_it   = _systems.find(sys_name);
    if (sys_it == _systems.end() || !sys_it->second.is_ready())
        return nullptr;
    return it->second.first;
}

auto system_registry::has_role_impl(std::string_view      system_name,
                                    std::type_info const& role_type) -> bool {
    return get_role_impl(system_name, role_type) != nullptr;
}

auto system_registry::get_role_impl(std::string_view      system_name,
                                    std::type_info const& role_type) -> void* {
    auto it = _systems.find(std::string(system_name));
    if (it == _systems.end())
        return nullptr;

    auto* instance = it->second.instance.get();
    if (!instance)
        return nullptr;

    // use dynamic_cast to check for role interfaces.
    std::type_index role_index(role_type);

    if (role_index == std::type_index(typeid(ir_bootstrap)))
        return dynamic_cast<ir_bootstrap*>(instance);
    if (role_index == std::type_index(typeid(ir_dependencies)))
        return dynamic_cast<ir_dependencies*>(instance);
    if (role_index == std::type_index(typeid(ir_runnable)))
        return dynamic_cast<ir_runnable*>(instance);
    if (role_index == std::type_index(typeid(ir_client)))
        return dynamic_cast<ir_client*>(instance);

    return nullptr;
}

// --- i_system_registry: plugin management ---
// —————————————————————————————————————————————————————————————————————————————

auto system_registry::load_plugin_library(std::string_view plugin_name)
    -> bool {
    // set loading context so static initializers track their source plugin.
    get_system_creator().set_loading_context(plugin_name);
    bool ok = _plugin_manager.load(plugin_name);
    get_system_creator().set_loading_context("");
    return ok;
}

auto system_registry::unload_plugin_library(std::string_view plugin_name)
    -> void {
    _plugin_manager.unload(plugin_name);
}

auto system_registry::is_plugin_loaded(std::string_view plugin_name) const
    -> bool {
    return _plugin_manager.is_loaded(plugin_name);
}

// --- i_system_registry: dynamic system initialization ---
// —————————————————————————————————————————————————————————————————————————————

auto system_registry::initialize_plugin_systems(std::string_view plugin_name)
    -> bool {
    auto& creator = get_system_creator();

    // create systems from pending entries.
    creator.create_from_pending(*this);

    // collect system names belonging to this plugin that are still pending.
    std::vector<std::string> new_system_names;
    {
        std::lock_guard lock(_plugin_tracking_mutex);
        for (const auto& [sys_name, src_plugin] : _system_to_plugin) {
            if (src_plugin == plugin_name) {
                if (_systems.contains(sys_name) &&
                    _systems[sys_name].state == system_state::pending) {
                    new_system_names.push_back(sys_name);
                }
            }
        }
    }

    if (new_system_names.empty()) {
        log()->trace("system_registry",
                     "no pending systems to initialize for plugin '{}'.",
                     plugin_name);
        return true;
    }

    log()->trace("system_registry",
                 "initializing {} system(s) from plugin '{}'...",
                 new_system_names.size(),
                 plugin_name);

    // initialize using system_creator.
    init_context ctx {
        .registry  = *this,
        .events    = _event,
        .jobs      = job(),
        .on_ready  = nullptr,
        .on_failed = nullptr,
    };

    auto result = creator.initialize_regular(ctx, new_system_names);

    if (!result.success()) {
        log()->error("system_registry",
                     "failed to initialize {} system(s) from plugin '{}'.",
                     result.failed,
                     plugin_name);
        return false;
    }

    // update cached role interfaces to include new runnables/clients.
    cache_role_interfaces();

    // publish plugin initialized event.
    std::string event_name = "plugin.initialized." + std::string(plugin_name);
    _event->publish_wait(event_name, nullptr);

    log()->info("system_registry",
                "initialized {} system(s) from plugin '{}'.",
                new_system_names.size(),
                plugin_name);

    return true;
}

auto system_registry::shutdown_plugin_systems(std::string_view plugin_name)
    -> void {
    // collect systems belonging to this plugin.
    std::vector<std::string> systems_to_shutdown;
    {
        std::lock_guard lock(_plugin_tracking_mutex);
        for (auto it = _system_to_plugin.begin();
             it != _system_to_plugin.end();) {
            if (it->second == plugin_name) {
                systems_to_shutdown.push_back(it->first);
                it = _system_to_plugin.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (systems_to_shutdown.empty()) {
        log()->trace("system_registry",
                     "no systems to shutdown for plugin '{}'.",
                     plugin_name);
        return;
    }

    log()->trace("system_registry",
                 "shutting down {} system(s) from plugin '{}'...",
                 systems_to_shutdown.size(),
                 plugin_name);

    // shutdown in reverse order.
    for (auto it = systems_to_shutdown.rbegin();
         it != systems_to_shutdown.rend();
         ++it) {
        const auto& name  = *it;
        auto        entry = _systems.find(name);
        if (entry != _systems.end()) {
            if (_event) {
                _event->publish("system.shutdown." + name);
            }

            entry->second.instance->on_shutdown();

            // remove interfaces belonging to this system.
            for (const auto& [type_idx, ptr] : entry->second.interfaces) {
                _interfaces.erase(type_idx);
            }

            _systems.erase(entry);

            log()->trace("system_registry",
                         "shutdown system '{}' (plugin unload).",
                         name);
        }
    }

    // update cached role interfaces.
    cache_role_interfaces();

    // publish plugin shutdown event.
    std::string event_name = "plugin.shutdown." + std::string(plugin_name);
    _event->publish_wait(event_name, nullptr);

    log()->info("system_registry",
                "shutdown {} system(s) from plugin '{}'.",
                systems_to_shutdown.size(),
                plugin_name);
}

// --- system management ---
// —————————————————————————————————————————————————————————————————————————————

auto system_registry::add_system(const std::string& name,
                                 system_entry&&     entry,
                                 const std::string& source_plugin) -> bool {
    if (_systems.contains(name)) {
        log()->error("system_registry",
                     "duplicate system registration: '{}'.",
                     name);
        return false;
    }

    // populate the interface map with all interfaces from this system.
    for (const auto& [type_idx, ptr] : entry.interfaces) {
        _interfaces.emplace(type_idx, std::make_pair(ptr, name));
    }

    // track source plugin if provided.
    if (!source_plugin.empty()) {
        std::lock_guard lock(_plugin_tracking_mutex);
        _system_to_plugin.emplace(name, source_plugin);
    }

    _systems.emplace(name, std::move(entry));
    return true;
}

auto system_registry::get_entry(const std::string& name) -> system_entry* {
    auto it = _systems.find(name);
    if (it == _systems.end())
        return nullptr;
    return &it->second;
}

auto system_registry::get_all_system_names() const
    -> std::vector<std::string> {
    std::vector<std::string> names;
    names.reserve(_systems.size());
    for (const auto& [name, entry] : _systems) {
        names.push_back(name);
    }
    return names;
}

auto system_registry::is_ready(const std::string& name) const -> bool {
    return is_system_ready(name);
}

auto system_registry::mark_system_ready(const std::string& name) -> void {
    auto it = _systems.find(name);
    if (it == _systems.end())
        return;

    // mark as ready.
    it->second.state = system_state::ready;

    // add to init order (thread-safe — may be called from job threads).
    {
        std::lock_guard lock(_init_order_mutex);
        _init_order.push_back(name);
    }

    log()->trace("system_registry",
                 "system '{}' is now ready. publishing event: system.ready.{}",
                 name,
                 name);

    // publish ready event to trigger dependents.
    if (_event) {
        _event->publish("system.ready." + name);
    }
}

auto system_registry::mark_system_failed(const std::string& name) -> void {
    auto it = _systems.find(name);
    if (it == _systems.end())
        return;

    it->second.state = system_state::failed;

    log()->error("system_registry",
                 "system '{}' failed to initialize. publishing failure event.",
                 name);

    if (_event) {
        _event->publish("system.failed." + name);
    }
}

// --- internal ---
// —————————————————————————————————————————————————————————————————————————————

auto system_registry::is_system_ready(std::string_view name) const -> bool {
    auto it = _systems.find(std::string(name));
    if (it == _systems.end())
        return false;
    return it->second.is_ready();
}

auto system_registry::cache_role_interfaces() -> void {
    // todo: replace with double-buffered atomic swap for lock-free reads.
    // currently not thread-safe if called while main loop is running.

    _runnables.clear();
    _client = nullptr;

    for (const auto& [name, entry] : _systems) {
        if (auto* runnable = dynamic_cast<ir_runnable*>(entry.instance.get())) {
            _runnables.push_back(runnable);
        }

        if (auto* client = dynamic_cast<ir_client*>(entry.instance.get())) {
            if (_client) {
                log()->warn("system_registry",
                            "multiple ir_client implementations found, "
                            "using '{}'. undefined behavior from here.",
                            name);
            } else {
                _client = client;
            }
        }
    }

    log()->trace("system_registry",
                 "cached {} runnables, client: {}",
                 _runnables.size(),
                 _client ? "found" : "not found");

    // configure main loop with cached interfaces.
    _main_loop.set_client(_client);
    auto* platform_if_ready_ptr = platform_if_ready() ? platform() : nullptr;
    if (platform_if_ready_ptr && _client) {
        platform_if_ready_ptr->set_close_policy(_client->close_policy());
    }
    _main_loop.set_platform(platform_if_ready_ptr);
    _main_loop.set_runnables(_runnables);
}

}  // namespace vent
