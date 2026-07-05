#pragma once
//
// world module.
// engine-internal world interface.
// ——————————————————————
//
// internal world system interface.

#include <_vent/world/ic_world.hpp>

namespace vent {

class i_world : public ic_world {
public:
    virtual ~i_world() = default;
};

}  // namespace vent
