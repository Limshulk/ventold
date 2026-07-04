# vent build system.
# ——————————————————————
# the event_bus module provides the event dispatching system.
# output: sdk/lib/libvent_event_bus.a

vent_create_module(
    NAME event_bus
    SOURCES
        src/event_bus_system.cpp
    INTERNAL_DEPS
        core
        log
        job
)
