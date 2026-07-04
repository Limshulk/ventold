#pragma once

#include <_vent/renderer/ic_pipeline.hpp>

namespace vent {

class i_pipeline : public ic_pipeline {
public:
    virtual ~i_pipeline() = default;
};

} // namespace vent
