#pragma once
//
// vent public sdk.
// renderer module interface.
// ——————————————————————
//
// client-facing marker interface for the renderer system.
//
// deliberately (almost) empty: the engine owns the frame loop. the renderer
// runs as an ir_runnable in the render phase — it iterates the platform's
// windows, extracts the world into render commands once per frame, and
// submits per window. clients describe the scene through the world system
// (mesh components, camera components) and never issue render calls.
// the only thing a client needs from this header is the system name, to
// declare the renderer as a dependency.

#include <_vent/vent_sdk.hpp>

namespace vent {

class ic_renderer {
public:
    virtual ~ic_renderer() = default;

    static constexpr std::string_view system_name = "vent.system.renderer";
};

}  // namespace vent
