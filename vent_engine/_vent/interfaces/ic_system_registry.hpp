#pragma once
//
// vent public sdk.
// client-faced system_registry interface.
// ——————————————————————
//
// provides type-based access to all registered system interfaces of the engine.
//
// usage:
//   auto* window = vent::system()->get<ic_window>(); // will assert if not found
//   bool as_deps = vent::system()->has_role<ir_dependencies>("vent.my_system");

namespace vent {

class ic_system_registry {
public:
    virtual ~ic_system_registry() = default;

protected:
};

}  // namespace vent