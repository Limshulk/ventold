#pragma once
//
// platform module.
// engine-facing platform interface.
// ——————————————————————
//
// extends ic_platform with engine-internal operations.

#include <_vent/interfaces/ic_platform.hpp>

namespace vent {

// forward declarations.
class i_window;

/// @brief internal interface for the platform system. extends ic_platform
///        with operations only used by engine internals.
class i_platform : public ic_platform {
public:
    virtual ~i_platform() = default;

    /// @brief get window as internal interface.
    /// @param window client-facing handle.
    /// @return internal interface pointer.
    [[nodiscard]]
    virtual auto get_internal_window(ic_window* window) -> i_window* = 0;
};

}  // namespace vent
