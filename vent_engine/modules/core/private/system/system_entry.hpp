#pragma once
//
// core module.
// system entry struct.
// ——————————————————————
//
// defines the data type the system_registry uses to store systems.

#include <_vent/vent_sdk.hpp>
#include <_vent/system/system_base.hpp>

#include <_vent/interfaces/ic_event_bus.hpp>
#include <core/system/interface_map.hpp>

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
struct system_entry {
    // --- ownership ---
    std::unique_ptr<system_base> instance;  ///< owning pointer to the system.

    interface_map interfaces;  ///< all interfaces this system owns. mapped with
                               ///< type & void*.

    // --- initialization state ---
    i32          stage = 0;  ///< current initialization stage.
    system_state state = system_state::pending;  ///< lifecycle state.

    // --- event waiting ---
    std::vector<std::string>
        pending_events;  ///< events blocking the next stage.
    std::unordered_map<std::string, subscription_id>
        event_subscriptions;  ///< active event subscriptions.

    // --- dependency tracking ---
    std::vector<std::string>
        pending_dependencies;  ///< dependencies yet to be initialized.

    // --- state queries ---

    /// @brief check if first initialization pass is complete (not pending).
    [[nodiscard]]
    constexpr auto has_started() const -> bool {
        return state != system_state::pending;
    }

    /// @brief check if system is fully initialized and ready.
    [[nodiscard]]
    constexpr auto is_ready() const -> bool {
        return state == system_state::ready;
    }

    /// @brief check if system initialization failed.
    [[nodiscard]]
    constexpr auto has_failed() const -> bool {
        return state == system_state::failed;
    }
};

}  // namespace vent