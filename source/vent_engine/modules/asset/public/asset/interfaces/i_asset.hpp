#pragma once
//
// asset module.
// engine-internal asset interface.
// ——————————————————————
//
// internal asset system interface. currently empty.

#include <_vent/asset/ic_asset.hpp>

namespace vent {

class i_asset : public ic_asset {
public:
    virtual ~i_asset() = default;
};

}  // namespace vent
