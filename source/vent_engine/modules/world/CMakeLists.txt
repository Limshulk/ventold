# vent build system.
# ——————————————————————
# the world module handles entity component simulation.
# output: sdk/lib/libvent_world.a

vent_create_module(
    NAME world
    SOURCES
        src/world_system.cpp
    INTERNAL_DEPS
        core
        log
)
