# vent build system.
# ——————————————————————
# the core module provides fundamental engine functionality.
# output: sdk/lib/libvent_core.a

vent_create_module(
    NAME core
    SOURCES
        # vent sdk sources
        src/_vent/accessors.cpp
        
        # core module sources
        src/vent_engine_entry.cpp
        
        src/event_bus_system.cpp
        src/main_loop.cpp
        src/plugin_manager.cpp
        src/system_creator.cpp
        src/system_registry.cpp
)
