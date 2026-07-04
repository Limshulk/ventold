// core module.
// system creator implementation.
// ——————————————————————
//
// implements the system_creator class.

#include <system/system_creator.hpp>
#include <system/system_registry.hpp>
#include <system/system_entry.hpp>

#include <core/system/registration.hpp>
#include <core/interfaces/ir_bootstrap.hpp>
#include <core/interfaces/ir_staged_system.hpp>

#include <_vent/accessors.hpp>
#include <_vent/client_registration.hpp>
#include <_vent/core/ir_dependencies.hpp>

#include <algorithm>
#include <cassert>

namespace vent {

// --- global instance ---
// —————————————————————————————————————————————————————————————————————————————

// thread-local context for tracking which plugin is currently being loaded. set
// before lib::load_library() so static initializers can capture it.
// this is somewhat hacky magically: on plugin_load, we set this to the plugin
// name. the same thread then calls get_pending_systems() by the static
// initializer directly after loading the library, so the pending registration
// entry (that is running on the same thread at static-init time of the library,
// directly after dlopen) can capture the plugin name. after that, we reset the
// thread-local to empty (manually!). this is kinda like passing a function
// argument, but there is just thread-local storage and no function.
thread_local std::string g_current_loading_plugin;

// global system_creator instance. created on first access.
static system_creator& get_creator_instance() {
    static system_creator instance;
    return instance;
}

auto get_system_creator() -> system_creator& {
    return get_creator_instance();
}

// --- global registration functions (called by macros) ---
// —————————————————————————————————————————————————————————————————————————————

VENT_API auto register_pending_system(pending_entry&& entry) -> void {
    // capture current loading context (empty for module systems).
    entry.source_plugin = g_current_loading_plugin;
    get_creator_instance().register_system(std::move(entry));
}

VENT_API auto register_client_factory(client_factory_fn factory) -> void {
    get_creator_instance().register_client(std::move(factory),
                                           g_current_loading_plugin);
}

// --- system_creator implementation ---
// —————————————————————————————————————————————————————————————————————————————

auto system_creator::set_loading_context(std::string_view plugin_name) -> void {
    g_current_loading_plugin = std::string(plugin_name);
}

auto system_creator::get_loading_context() const -> std::string_view {
    return g_current_loading_plugin;
}

auto system_creator::register_system(pending_entry&& entry) -> void {
    std::lock_guard lock(_pending_mutex);
    _pending_systems.push_back(std::move(entry));
}

auto system_creator::register_client(client_factory_fn factory,
                                     std::string       source_plugin) -> void {
    std::lock_guard lock(_pending_mutex);
    _client_factory       = std::move(factory);
    _client_source_plugin = std::move(source_plugin);
}

auto system_creator::create_from_pending(system_registry& registry) -> u32 {
    std::lock_guard lock(_pending_mutex);

    if (_pending_systems.empty()) {
        log()->trace("system_creator", "no pending systems to create.");
        return 0;
    }
    log()->trace("system_creator",
                 "called create_from_pending() with {} pending systems.",
                 _pending_systems.size());

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
            auto dep_span = deps->dependencies();
            se.pending_dependencies =
                std::vector<std::string>(dep_span.begin(), dep_span.end());
        }

        // add to registry.
        if (registry.add_system(name, std::move(se), entry.source_plugin)) {
            count++;
        }

        log()->trace(
            "system_creator",
            "  [{}/{}]: pending system '{}' created and added to registry.",
            count,
            _pending_systems.size(),
            name);
    }

    log()->trace("system_creator",
                 "{} systems created from pending registrations.",
                 count);

    // clear pending list.
    _pending_systems.clear();
    return count;
}

auto system_creator::create_client(system_registry& registry) -> bool {
    std::lock_guard lock(_pending_mutex);

    if (!_client_factory) {
        log()->trace("system_creator", "no client factory registered.");
        return false;
    }

    // create the client instance.
    auto client = _client_factory();

    // create system_entry for client.
    system_entry se;
    se.instance = std::move(client);

    // set pending dependencies if client implements ir_dependencies.
    if (auto* deps = dynamic_cast<ir_dependencies*>(se.instance.get())) {
        auto dep_span = deps->dependencies();
        se.pending_dependencies =
            std::vector<std::string>(dep_span.begin(), dep_span.end());
    }

    // add to registry with name "client".
    if (!registry.add_system("client", std::move(se), _client_source_plugin)) {
        log()->error("system_creator", "failed to add client to registry.");
        return false;
    }

    // clear factory to prevent re-creation.
    _client_factory = nullptr;
    _client_source_plugin.clear();

    log()->trace("system_creator",
                 "client created and added to registry as 'client'.");
    return true;
}

auto system_creator::categorize_systems(system_registry&             registry,
                                        std::span<const std::string> all_names,
                                        std::vector<std::string>& bootstrap_out,
                                        std::vector<std::string>& regular_out)
    -> void {
    bootstrap_out.clear();
    regular_out.clear();

    log()->trace("system_creator", "categorizing {} systems...", all_names.size());

    for (const auto& name : all_names) {
        if (registry.has_role<ir_bootstrap>(name)) {
            bootstrap_out.push_back(name);
        } else {
            regular_out.push_back(name);
        }
    }

    log()->trace("system_creator", "  {} bootstrap systems, {} regular systems found.", bootstrap_out.size(), regular_out.size());

    // sort bootstrap systems by priority (lowest first)
    std::sort(bootstrap_out.begin(),
              bootstrap_out.end(),
              [&registry](const std::string& a, const std::string& b) {
                  auto* entry_a = registry.get_entry(a);
                  auto* entry_b = registry.get_entry(b);
                  auto* a_impl =
                      entry_a
                          ? dynamic_cast<ir_bootstrap*>(entry_a->instance.get())
                          : nullptr;
                  auto* b_impl =
                      entry_b
                          ? dynamic_cast<ir_bootstrap*>(entry_b->instance.get())
                          : nullptr;

                  i32 p_a = a_impl ? a_impl->bootstrap_priority() : 0;
                  i32 p_b = b_impl ? b_impl->bootstrap_priority() : 0;
                  return p_a < p_b;
              });
}

auto system_creator::get_startable_systems(system_registry& registry,
                                           std::span<const std::string> names,
                                           bool include_bootstrap)
    -> std::vector<std::string> {
    std::vector<std::string> startable;

    // if no names provided, get all systems from registry.
    std::vector<std::string> all_names;
    if (names.empty()) {
        all_names = registry.get_all_system_names();
        names     = all_names;
    }

    for (const auto& name : names) {
        auto* entry = registry.get_entry(name);
        if (!entry)
            continue;

        // skip bootstrap systems unless requested.
        if (!include_bootstrap && registry.has_role<ir_bootstrap>(name))
            continue;

        if (entry->pending_dependencies.empty()) {
            startable.push_back(name);
        }
    }

    return startable;
}

auto system_creator::initialize_bootstrap(init_context&                ctx,
                                          std::span<const std::string> names)
    -> bool {
    log()->trace(
        "system_creator", "initializing {} bootstrap systems...", names.size());

    int count = 0;
    for (const auto& name : names) {
        auto* entry = ctx.registry.get_entry(name);
        if (!entry) {
            log()->error(
                "system_creator", "bootstrap system '{}' not found.", name);
            if (ctx.on_failed)
                ctx.on_failed(name);
            return false;
        }
        count++;

        log()->trace("system_creator",
                     "  [{}/{}]: initializing bootstrap system '{}'...",
                     count,
                     names.size(),
                     name);

        // bootstrap systems use single-stage initialization.
        auto result = entry->instance->on_initialization();

        if (result == system_initialization_status::failed) {
            log()->error("system_creator",
                         "bootstrap system '{}' failed to initialize.",
                         name);
            ctx.registry.mark_system_failed(name);
            if (ctx.on_failed)
                ctx.on_failed(name);
            return false;
        }

        ctx.registry.mark_system_ready(name);
        if (ctx.on_ready)
            ctx.on_ready(name);

        log()->trace("system_creator",
                     "  [{}/{}]: bootstrap system '{}' initialized.",
                     count,
                     names.size(),
                     name);
    }

    log()->trace("system_creator",
                 "all {} bootstrap systems initialized successfully.",
                 names.size());

    return true;
}

auto system_creator::initialize_one(system_registry&   registry,
                                    const std::string& name)
    -> system_init_command::action {
    auto* entry = registry.get_entry(name);
    if (!entry) {
        return system_init_command::action::failed;
    }

    if (auto* staged = dynamic_cast<ir_staged_system*>(entry->instance.get())) {
        log()->trace("system_creator",
                     "system '{}' is an ir_staged_system! starting loop...",
                     name);
        // run initialization loop until complete, failed, or awaiting events.
        while (true) {
            auto result = staged->on_staged_initialization(entry->stage);

            switch (result.state) {
                case system_init_command::action::proceed:
                    log()->trace("system_creator", "  system '{}' stage {} returned proceed.", name, entry->stage);
                    entry->stage++;
                    continue;

                case system_init_command::action::await_event:
                    log()->trace("system_creator", "  system '{}' stage {} returned await_event.", name, entry->stage);
                    entry->pending_events = std::move(result.events);
                    return system_init_command::action::await_event;

                case system_init_command::action::complete:
                    log()->trace("system_creator", "  system '{}' stage {} returned complete.", name, entry->stage);
                    return system_init_command::action::complete;

                case system_init_command::action::failed:
                    log()->trace("system_creator", "  system '{}' stage {} returned failed.", name, entry->stage);
                    return system_init_command::action::failed;
            }
        }
    } else {
        log()->trace("system_creator",
                     "system '{}' is not an ir_staged_system. calling "
                     "on_initialization().",
                     name);
        auto result = entry->instance->on_initialization();
        return result == system_initialization_status::success
                   ? system_init_command::action::complete
                   : system_init_command::action::failed;
    }

    return system_init_command::action::failed;
}

auto system_creator::initialize_regular(init_context&                ctx,
                                        std::span<const std::string> names)
    -> batch_init_result {
    batch_init_result result;
    result.total = static_cast<u32>(names.size());

    if (names.empty()) {
        return result;
    }

    // regular init requires event bus system.
    assert(ctx.events && "init_context.events must be set for regular init.");

    // counters for parallel initialization.
    std::atomic<u32> first_pass_count {0};
    std::atomic<u32> fail_count {0};

    // callback when a system completes first pass.
    auto on_first_pass = [&first_pass_count]() {
        first_pass_count.fetch_add(1, std::memory_order_release);
        first_pass_count.notify_one();
    };

    // helper to fire a system's initialization on job thread.
    // captures `this` for initialize_one, but that's safe as this function
    // blocks until all inits complete.
    auto fire_init = [this, &ctx, &fail_count, &on_first_pass](
                         const std::string& sys_name) {
        ctx.jobs->fire([this, &ctx, sys_name, &fail_count, &on_first_pass]() {
            auto outcome = initialize_one(ctx.registry, sys_name);

            switch (outcome) {
                case system_init_command::action::complete:
                    ctx.registry.mark_system_ready(sys_name);
                    if (ctx.on_ready)
                        ctx.on_ready(sys_name);
                    on_first_pass();
                    break;

                case system_init_command::action::await_event:
                    // system is waiting for events. first pass is done.
                    // todo: setup event subscriptions for continuation.
                    on_first_pass();
                    break;

                case system_init_command::action::failed:
                    ctx.registry.mark_system_failed(sys_name);
                    if (ctx.on_failed)
                        ctx.on_failed(sys_name);
                    fail_count.fetch_add(1, std::memory_order_release);
                    on_first_pass();
                    break;

                default: break;
            }
        });
    };

    // setup dependency events and track which systems can start.
    std::vector<std::string> startup_systems;

    for (const auto& name : names) {
        log()->trace(
            "system_creator", "checking dependencies for system '{}'...", name);
        auto* entry = ctx.registry.get_entry(name);
        if (!entry)
            continue;

        // skip if no dependencies.
        if (entry->pending_dependencies.empty()) {
            log()->trace("system_creator",
                         "  system '{}' has no dependencies and is ready to "
                         "start initialization.",
                         name);
            startup_systems.push_back(name);
            continue;
        }

        // remove already-ready dependencies.
        auto it = entry->pending_dependencies.begin();
        while (it != entry->pending_dependencies.end()) {
            const auto& dep = *it;
            if (ctx.registry.is_ready(dep)) {
                log()->trace(
                    "system_creator", "  dependency '{}' already ready.", dep);
                it = entry->pending_dependencies.erase(it);
            } else {
                log()->trace(
                    "system_creator", "  dependency '{}' pending.", dep);
                ++it;
            }
        }

        if (entry->pending_dependencies.empty()) {
            startup_systems.push_back(name);
            log()->trace("system_creator",
                         "  system '{}' ready to start initialization.",
                         name);
        } else {
            // subscribe to dependency ready events.
            for (const auto& dep : entry->pending_dependencies) {
                std::string event_name = "system.ready." + dep;
                log()->trace("system_creator",
                             "  subscribing to dependency event '{}'.",
                             event_name);
                auto sub_id = ctx.events->subscribe(
                    event_name,
                    [&ctx, name, dep, &fire_init](std::string_view,
                                                  void*) mutable {
                        auto* e = ctx.registry.get_entry(name);
                        if (!e)
                            return;
                        log()->trace("system_creator",
                                     "  dependency '{}' ready event recieved "
                                     "for system '{}'.",
                                     dep,
                                     name);

                        // remove this dependency.
                        std::erase(e->pending_dependencies, dep);

                        // if all deps ready, fire initialization.
                        if (e->pending_dependencies.empty()) {
                            log()->trace("system_creator",
                                         "  all dependencies ready for system "
                                         "'{}', starting initialization.",
                                         name);
                            fire_init(name);
                        }
                    });

                auto* e = ctx.registry.get_entry(name);
                if (e) {
                    e->event_subscriptions.emplace(dep, sub_id);
                }
            }
        }
    }

    log()->trace("system_creator",
                 "{} systems ready to start, {} waiting for dependencies.",
                 startup_systems.size(),
                 names.size() - startup_systems.size());

    // fire initial batch.
    log()->trace("system_creator", "firing initialization for systems:");
    for (const auto& name : startup_systems) {
        log()->trace("system_creator", "  - '{}'", name);
        fire_init(name);
    }

    log()->trace("system_creator",
                 "wait for all fired systems to complete first pass.");
    // wait for all systems to complete first pass.
    while (first_pass_count.load(std::memory_order_acquire) < result.total) {
        // log progress.
        log()->trace("system_creator",
                     "  progress: {} / {} systems completed.",
                     first_pass_count.load(std::memory_order_relaxed),
                     result.total);
        first_pass_count.wait(first_pass_count.load(std::memory_order_relaxed),
                              std::memory_order_acquire);
    }

    result.failed = fail_count.load(std::memory_order_relaxed);
    result.ready  = result.total - result.failed;  // simplified for now.

    return result;
}

auto system_creator::setup_dependency_events(
    init_context&                           ctx,
    std::span<const std::string>            names,
    std::function<void(const std::string&)> on_all_deps_ready) -> void {
    // implementation delegated to initialize_regular for now.
    // this function is for future modular use.
    (void) ctx;
    (void) names;
    (void) on_all_deps_ready;
}

auto system_creator::setup_init_events(init_context&                ctx,
                                       const std::string&           name,
                                       std::span<const std::string> events,
                                       std::function<void()> on_all_events)
    -> void {
    // implementation for staged initialization event waiting.
    // todo: implement when needed.
    (void) ctx;
    (void) name;
    (void) events;
    (void) on_all_events;
}

}  // namespace vent
