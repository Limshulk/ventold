#pragma once
//
// core module.
// system entry struct.
// ——————————————————————
//
// defines the data type the system_registry uses to store systems.

#include <_vent/vent_sdk.hpp>
#include <_vent/system/system_base.hpp>

#include <_vent/event_bus/ic_event_bus.hpp>
#include <core/system/interface_map.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace vent {

/// @brief lifecycle state of a system within the registry.
/// transitions: pending → awaiting_event → ready
///              pending → ready (if no events to wait for)
///              pending → failed (if initialization fails)
///              awaiting_event → ready (once all events received)
///              awaiting_event → failed (if subsequent stage fails)
enum class system_state : u8 {
    pending,         ///< not yet started initialization.
    awaiting_event,  ///< first pass done, waiting for external events.
    ready,           ///< fully initialized and operational.
    failed           ///< initialization failed.
};

/// @brief runtime entry for a registered system. stores the system instance and
/// its lifecycle state.
///
/// @note `state` is atomic because it is written from job-worker threads
/// (mark_system_ready / mark_system_failed during parallel init) and read
/// concurrently from other workers (dependency checks). a plain enum would be a
/// data race — formally UB, and the compiler may assume no concurrent access.
/// because std::atomic is not movable, and this struct lives in an
/// unordered_map that we populate with emplace(std::move(entry)), we provide an
/// explicit move constructor / assignment that transfers the loaded value.
struct system_entry {
    // --- ownership ---
    std::unique_ptr<system_base> instance;  ///< owning pointer to the system.

    interface_map interfaces;  ///< all interfaces this system owns. mapped with
                               ///< type & void*.

    // --- initialization state ---
    i32                       stage = 0;  ///< current initialization stage.
    std::atomic<system_state> state {
        system_state::pending};  ///< lifecycle state (see note above).

    // --- event waiting ---
    std::vector<std::string>
        pending_events;  ///< events blocking the next stage.
    std::unordered_map<std::string, subscription_id>
        event_subscriptions;  ///< active event subscriptions.

    // --- dependency tracking ---
    std::vector<std::string>
        pending_dependencies;  ///< dependencies yet to be initialized.

    // --- special members ---
    // move-only (unique_ptr) plus a hand-written atomic transfer. the entry is
    // only ever moved into its map node once, so a relaxed load/store is fine.
    system_entry() = default;
    system_entry(system_entry&& other) noexcept
        : instance(std::move(other.instance)),
          interfaces(std::move(other.interfaces)),
          stage(other.stage),
          state(other.state.load(std::memory_order_relaxed)),
          pending_events(std::move(other.pending_events)),
          event_subscriptions(std::move(other.event_subscriptions)),
          pending_dependencies(std::move(other.pending_dependencies)) {}
    auto operator=(system_entry&& other) noexcept -> system_entry& {
        if (this != &other) {
            instance   = std::move(other.instance);
            interfaces = std::move(other.interfaces);
            stage      = other.stage;
            state.store(other.state.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
            pending_events       = std::move(other.pending_events);
            event_subscriptions  = std::move(other.event_subscriptions);
            pending_dependencies = std::move(other.pending_dependencies);
        }
        return *this;
    }

    // --- state queries ---

    /// @brief check if first initialization pass is complete (not pending).
    [[nodiscard]]
    auto has_started() const -> bool {
        return state.load(std::memory_order_acquire) != system_state::pending;
    }

    /// @brief check if system is fully initialized and ready.
    [[nodiscard]]
    auto is_ready() const -> bool {
        return state.load(std::memory_order_acquire) == system_state::ready;
    }

    /// @brief check if system initialization failed.
    [[nodiscard]]
    auto has_failed() const -> bool {
        return state.load(std::memory_order_acquire) == system_state::failed;
    }
};

}  // namespace vent