# vent build system.
# ——————————————————————
# the asset module handles file loading and virtual file system.
# output: sdk/lib/libvent_asset.a

vent_create_module(
    NAME asset
    SOURCES
        src/asset_system.cpp
    INTERNAL_DEPS
        core
        log
)

# the asset module is the only consumer of stb / tinyobjloader. include them
# PRIVATE here (not globally in vent_module.cmake) so these third-party headers
# do not leak into other modules or the sdk surface.
target_include_directories(vent_asset PRIVATE "${VENT_ENGINE_DIR}/third_party")
