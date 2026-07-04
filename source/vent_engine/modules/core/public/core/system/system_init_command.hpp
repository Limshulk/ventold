#pragma once
//
// core module.
// system init command.
// ——————————————————————
//
// return type for internal staged system initialization. indicates what the
// system registry should do next (proceed, wait, complete, or fail).

#include <string>
#include <vector>

namespace vent {

/// @brief command from a staged system initialization step.
struct system_init_command {

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
    static auto proceed() -> system_init_command {
        return {.state = action::proceed};
    }

    /// @brief wait for one event before proceeding.
    static auto await_event(std::string event) -> system_init_command {
        return {.state = action::await_event, .events = {std::move(event)}};
    }

    /// @brief wait for multiple events before proceeding.
    static auto await_events(std::vector<std::string> events)
        -> system_init_command {
        return {.state = action::await_event, .events = std::move(events)};
    }

    /// @brief complete initialization.
    static auto complete() -> system_init_command {
        return {.state = action::complete};
    }

    /// @brief indicate initialization failure.
    static auto failed() -> system_init_command {
        return {.state = action::failed};
    }
};

}  // namespace vent
