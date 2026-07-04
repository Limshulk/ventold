# vent build system.
# ——————————————————————
# the asset module handles file loading and virtual file system.
# output: sdk/lib/libvent_asset.a

vent_create_module(
    NAME asset
    SOURCES
        src/file_system.cpp
        src/asset_system.cpp
    INTERNAL_DEPS
        core
        log
)
