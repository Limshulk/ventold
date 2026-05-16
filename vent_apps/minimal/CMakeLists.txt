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
        #log
        #job
        #platform
        #renderer
    PLUGINS
        #vulkan_backend     # copied, loaded dynamically by renderer.
    # STARTUP_PLUGINS     # (empty - no plugins need auto-loading).
    SOURCES
        src/minimal.cpp
)
