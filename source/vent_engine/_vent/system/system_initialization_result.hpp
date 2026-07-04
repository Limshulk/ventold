#pragma once
//
// vent public sdk.
// system initialization result.
// ——————————————————————
//
// return type for system initialization. indicates what the system registry
// should do next (proceed, wait, complete, or fail).

#include <string>
#include <vector>

namespace vent {

/// @brief result from a system initialization step.
struct system_initialization_result {

    /// @brief possible actions after initialization step.
    enum class action {
        proceed,      ///< immediately call next initialization step.
        await_event,  ///< wait for an event before proceeding.
        complete,     ///< system initialization is complete.
        failed        ///< system initialization failed.
    };

    action state = action::complete;       ///< action to take after init.
    std::vector<std::string> events = {};  ///< events to wait for (if any).

    // --- factory functions ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief proceed to next initialization step.
    static auto proceed() -> system_initialization_result {
        return {.state = action::proceed};
    }

    /// @brief wait for one event before proceeding.
    static auto await_event(std::string event) -> system_initialization_result {
        return {.state = action::await_event, .events = {std::move(event)}};
    }

    /// @brief wait for multiple events before proceeding.
    static auto await_events(std::vector<std::string> events)
        -> system_initialization_result {
        return {.state = action::await_event, .events = std::move(events)};
    }

    /// @brief complete initialization.
    static auto complete() -> system_initialization_result {
        return {.state = action::complete};
    }

    /// @brief indicate initialization failure.
    static auto failed() -> system_initialization_result {
        return {.state = action::failed};
    }
};

}  // namespace vent
