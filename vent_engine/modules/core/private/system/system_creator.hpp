#pragma once
//
// core module.
// system creator.
// ——————————————————————
//
// owns pending system registrations and provides system creation functionality.
// encapsulates the global pending lists and provides thread-safe registration.
//
// note the differenciation: loading, creating, initializing.
// - loading: loading plugin libraries from disk. modules are always loaded
//     as they are linked into the main libvent_engine.so. loading plugins is
//     neither handled nor called by the system_creator.
// - creating: instantiating system objects from registered factories. takes
//     a few microseconds i guess.
// - initializing: running the potentially complex initialization logic of
//     each system. can take arbitrary time depending on what the system does.

#include <_vent/vent_sdk.hpp>
#include <_vent/system/initialization_result.hpp>
#include <_vent/interfaces/ic_job.hpp>
#include <_vent/interfaces/ic_event_bus.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace vent {

// forward declarations.
class system_registry;
class client_base;
struct pending_entry;

// --- batch initialization result ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief result of batch system initialization. tracks batch
/// statistics across multiple systems.
struct batch_init_result {
    u32 total    = 0;  ///< total systems attempted.
    u32 ready    = 0;  ///< systems fully ready.
    u32 awaiting = 0;  ///< systems waiting for events.
    u32 failed   = 0;  ///< systems that failed.

    /// @brief check if initialization succeeded (no failures). awaiting events
    /// means the current stage is initialized successfully.
    [[nodiscard]]
    constexpr auto success() const -> bool {
        return failed == 0;
    }

    /// @brief check if batch completed without failures.
    [[nodiscard]]
    constexpr auto is_complete() const -> bool {
        return (ready + failed) == total;
    }
};

// --- initialization context ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief context required for initialization operations.
/// events and jobs are nullable pointers: during bootstrap phase, event_bus
/// and job_system don't exist yet (they ARE bootstrap systems). after bootstrap
/// completes, both must be set for regular initialization.
struct init_context {
    system_registry& registry;       ///< system storage.
    ic_event_bus*    events = nullptr;  ///< for dependency/init events (null during bootstrap).
    ic_job*          jobs   = nullptr;  ///< for parallel initialization (null during bootstrap).

    /// @brief callback when a system becomes ready.
    std::function<void(const std::string&)> on_ready;

    /// @brief callback when a system fails to initialize.
    std::function<void(const std::string&)> on_failed;
};

// --- system_creator class ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief client factory function type.
using client_factory_fn = std::function<std::unique_ptr<client_base>()>;

/// @brief owns pending system registrations and provides creation
/// functionality.
///
/// manages the pending lists populated by static registration macros
/// and provides functions for creating system instances from them.
/// singleton-like class, the instance is created and owned by system_registry
/// thread safety:
/// - registration functions are thread-safe (use internal mutex).
/// - creation functions should only be called from main thread during init.
class system_creator {
public:
    system_creator()  = default;
    ~system_creator() = default;

    VENT_NO_COPY_MOVE(system_creator);

private:
    /// @brief mutex for thread-safe access to pending lists.
    std::mutex _pending_mutex;

    /// @brief pending system registrations.
    std::vector<pending_entry> _pending_systems;

    /// @brief pending client factory.
    client_factory_fn _client_factory;

public:
    // --- plugin loading context ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief set the current loading context for tracking plugin origins. call
    /// before loading a plugin library, clear after load completes.
    /// @param plugin_name name of the plugin being loaded (empty to clear).
    auto set_loading_context(std::string_view plugin_name) -> void;

    /// @brief get the current loading context.
    /// @return name of the plugin being loaded, or empty if none.
    [[nodiscard]]
    auto get_loading_context() const -> std::string_view;

    // --- registration (called by static initializers) ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief register a pending system entry. called by VENT_SYSTEM macro
    /// during static initialization.
    /// @param entry pending entry containing factory and interface mapper.
    auto register_system(pending_entry&& entry) -> void;

    /// @brief register a client factory.
    /// called by VENT_CLIENT macro during static initialization.
    /// @param factory function that creates the client instance.
    auto register_client(client_factory_fn factory) -> void;

    // --- system creation ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief create system instances from pending registrations.
    /// @param registry registry to add created systems to.
    /// @return number of systems created.
    auto create_from_pending(system_registry& registry) -> u32;

    /// @brief create client instance from pending registration.
    /// @param registry registry to add the client to.
    /// @return true if client was created successfully.
    auto create_client(system_registry& registry) -> bool;

    // --- system categorization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief categorize systems into bootstrap and regular groups. bootstrap
    /// systems implement ir_bootstrap, have no dependencies and are initialized
    /// first, sequentially. regular systems are initialized in parallel with
    /// dependency resolution.
    /// @param registry source registry containing systems.
    /// @param all_names all system names to categorize.
    /// @param bootstrap_out output: names of bootstrap systems.
    /// @param regular_out output: names of regular systems.
    auto categorize_systems(system_registry&             registry,
                            std::span<const std::string> all_names,
                            std::vector<std::string>&    bootstrap_out,
                            std::vector<std::string>&    regular_out) -> void;

    /// @brief get systems that have no pending dependencies. these systems can
    /// start initialization immediately.
    /// @param registry source registry.
    /// @param names optional: system names to check. if empty, checks all
    /// systems in the registry.
    /// @param include_bootstrap if true, also returns bootstrap systems.
    /// default is false (only regular systems).
    /// @return names of systems ready to start (no pending dependencies).
    [[nodiscard]]
    auto get_startable_systems(system_registry&             registry,
                               std::span<const std::string> names = {},
                               bool include_bootstrap             = false)
        -> std::vector<std::string>;

    // --- initialization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize bootstrap systems sequentially. bootstrap systems are
    /// initialized one at a time on the calling thread. they do not support
    /// staged initialization or event waiting.
    /// @param ctx initialization context.
    /// @param names bootstrap system names to initialize.
    /// @return true if all bootstrap systems initialized successfully.
    auto initialize_bootstrap(init_context&                ctx,
                              std::span<const std::string> names) -> bool;

    /// @brief initialize regular systems with dependency resolution. systems
    /// are initialized in parallel using the job system. dependencies are
    /// tracked and systems start when their dependencies become ready. supports
    /// staged initialization and event waiting.
    /// @param ctx initialization context.
    /// @param names regular system names to initialize.
    /// @return batch_init_result with counts of ready/awaiting/failed systems.
    auto initialize_regular(init_context&                ctx,
                            std::span<const std::string> names)
        -> batch_init_result;

    /// @brief initialize a single system through its stages. called by job
    /// threads during parallel initialization.
    /// @param registry system registry.
    /// @param name system name to initialize.
    /// @return action indicating outcome (complete, await_event, failed).
    auto initialize_one(system_registry& registry, const std::string& name)
        -> initialization_result::action;

    // --- dependency event management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief setup dependency event subscriptions for systems. subscribes to
    /// system.ready.<dependency> events for each system's pending dependencies.
    /// @param ctx initialization context.
    /// @param names system names to setup.
    /// @param on_all_deps_ready callback when a system has all deps ready.
    auto setup_dependency_events(
        init_context&                           ctx,
        std::span<const std::string>            names,
        std::function<void(const std::string&)> on_all_deps_ready) -> void;

    /// @brief setup init event subscriptions for a system waiting for events.
    /// subscribes to events the system is waiting for during staged init.
    /// @param ctx initialization context.
    /// @param name system name.
    /// @param events events the system is waiting for.
    /// @param on_all_events callback when all events have been received.
    auto setup_init_events(init_context&                ctx,
                           const std::string&           name,
                           std::span<const std::string> events,
                           std::function<void()>        on_all_events) -> void;
};

// --- global registration functions ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief register a pending system entry.
/// called by VENT_SYSTEM macro during static initialization.
/// delegates to the global system_creator instance.
/// @param entry pending entry containing factory and interface mapper.
VENT_API auto register_pending_system(pending_entry&& entry) -> void;

/// @brief register a client factory.
/// called by VENT_CLIENT macro during static initialization.
/// delegates to the global system_creator instance.
/// @param factory function that creates the client instance.
VENT_API auto register_client_factory(client_factory_fn factory) -> void;

/// @brief get the global system_creator instance.
/// @return reference to the global system_creator.
auto get_system_creator() -> system_creator&;

}  // namespace vent
