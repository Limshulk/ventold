#pragma once
//
// core module.
// bootstrap role interface.
// ——————————————————————
//
// marker interface for systems that need to initialize early. bootstrap systems
// are initialized sequentially before regular systems. they typically include
// core infrastructure like logging, job system, etc.
//
// clients cannot implement custom bootstrap systems: they are initialized
// before the client library is even loaded.
//
// usage:
//   class my_core_system : public system_base, public ir_bootstrap { ... };

#include <_vent/vent_sdk.hpp>

namespace vent {

/// @brief marker interface for bootstrap systems. systems implementing this
/// interface initialize before regular systems.
class ir_bootstrap {
public:
    virtual ~ir_bootstrap() = default;
    /// @brief priority level for initialization. lower values initialize first.
    /// @return the priority level.
    [[nodiscard]]
    virtual auto bootstrap_priority() const -> i32 {
        return 0;
    }
};

}  // namespace vent
