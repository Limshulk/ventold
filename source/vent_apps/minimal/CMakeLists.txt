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
    PLUGINS
        vulkan_backend     # copied, loaded dynamically by renderer.
    # STARTUP_PLUGINS     # (empty - no plugins need auto-loading).
    SOURCES
        src/minimal.cpp
)

vent_compile_shaders(
    TARGET minimal
    OUTPUT_DIR "${VENT_APPS_OUTPUT_DIR}/minimal/assets/shaders"
    SOURCES
        assets/shaders/dummy.slang
        assets/shaders/shader.slang
)
