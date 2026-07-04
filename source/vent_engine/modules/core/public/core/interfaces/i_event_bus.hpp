#pragma once
//
// core module.
// engine-facing event_bus interface.
// ——————————————————————
//
// extends the client-facing ic_event_bus by additional, engine-internal
// functionality.

#include <_vent/interfaces/ic_event_bus.hpp>

namespace vent {

class i_event_bus : public ic_event_bus {
public:

    virtual ~i_event_bus() = default;

    // --- internal functionality ---
    // —————————————————————————————————————————————————————————————————————————
    // nothing here yet.
};

}  // namespace vent