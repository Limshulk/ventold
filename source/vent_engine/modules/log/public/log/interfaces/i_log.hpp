#pragma once
//
// log module.
// engine-facing log system interface.
// ——————————————————————
//
// extends ic_log with engine-internals.

#include <_vent/core/ic_log.hpp>

namespace vent {

class i_log : public ic_log {
public:
    virtual ~i_log() = default;

    // --- internal functionality ---
    // —————————————————————————————————————————————————————————————————————————

    // nothing here yet.
};

}  // namespace vent