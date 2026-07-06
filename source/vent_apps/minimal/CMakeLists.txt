# vent build system.
# ——————————————————————
# example client application demonstrating the vent engine setup.
# 
# the vulkan_backend plugin is listed in PLUGINS (copied to app dir) but NOT in
# STARTUP_PLUGINS, so it won't be auto-loaded. instead, the renderer system loads
# it dynamically when it initializes.

vent_create_client(
    NAME minimal
    MODULES
        core
        event_bus
        log
        job
        platform
        renderer
        asset
        world
    PLUGINS
        vulkan_backend     # copied, loaded dynamically by renderer.
    # STARTUP_PLUGINS     # (empty - no plugins need auto-loading).
    SOURCES
        src/minimal.cpp
)

# no app shaders anymore: the engine ships its default + error shaders in
# engine_assets/ (compiled and copied by vent_create_client). apps only add
# shaders here once they have custom materials.

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/texture.jpg"
     DESTINATION "${VENT_APPS_OUTPUT_DIR}/minimal/assets")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/viking_room.obj"
     DESTINATION "${VENT_APPS_OUTPUT_DIR}/minimal/assets")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/viking_room.png"
     DESTINATION "${VENT_APPS_OUTPUT_DIR}/minimal/assets")
