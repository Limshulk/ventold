# vent build system.
# ——————————————————————
# the rendering frontend.

vent_create_module(
    NAME renderer
    SOURCES
        # vent sdk sources
        
        # renderer module sources
        src/renderer.cpp
    INTERNAL_DEPS
        core
)
