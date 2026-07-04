# vent build system
# ——————————————————————
# entry point for building the vent project

# the sdk is a standalone package that can be distributed to users.
# it contains:
#   - public headers   (sdk/include/_vent/)
#   - static libraries (sdk/lib/libvent_<module>.a)
#   - plugins          (sdk/plugins/libvent_<plugin>.so)
#   - templates        (sdk/templates/launcher.cpp)

# output structure:
#   build/<platform>/<compiler>/<config>/sdk/
#   ├── include/
#   │   └── _vent/              public sdk headers.
#   ├── lib/
#   │   ├── libvent_core.a      core module.
#   │   ├── libvent_job.a       job module.
#   │   └── ...                 other modules.
#   ├── plugins/
#   │   ├── libvent_renderer_vulkan.so
#   │   └── ...                 hot-reloadable plugins.
#   └── templates/
#       └── launcher.cpp        launcher template for client apps.
#
# cmake artifacts are kept separate from sdk output:
#   build/cmake-<preset>/       ninja files, object files, cache, etc.
#
# usage:
#   cmake --preset linux-debug
#   cmake --build --preset linux-debug
#   cmake --build --preset linux-debug --target VENT_SDK

cmake_minimum_required(VERSION 3.20)

project(vent_workspace
    VERSION 0.1.0
    DESCRIPTION "vent engine"
    LANGUAGES CXX
)

# --- global c++ settings ---
# c++23 is required. no extensions, no c++20 modules (still unstable).

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# generate compile_commands.json for ide/lsp integration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/build/compile_commands/compile_commands.json"
    VERBATIM
)

# --- include vent build system helpers ---
# the cmake/ directory contains helper modules:
#   - vent_global.cmake: platform detection, paths, utility functions.
#   - vent_module.cmake: vent_create_module() for static library modules.
#   - vent_plugin.cmake: vent_create_plugin() for shared library plugins.
#   - vent_client.cmake: vent_create_client() for client applications.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(vent_global)
include(vent_module)
include(vent_plugin)
include(vent_client)
include(vent_shader)

# --- build the sdk ---
# the engine subdirectory builds all modules and plugins into the sdk.

add_subdirectory(source/vent_engine vent_engine)

# --- build applications ---
# applications in vent_apps/ are always configured, but only built when
# explicitly requested via VENT_APPS or VENT_ALL targets.

add_subdirectory(source/vent_apps vent_apps)

# --- build targets ---
# three main targets for building:
#   VENT_SDK  - sdk only (headers, modules, plugins, templates).
#   VENT_APPS - applications only (requires sdk to be built first).
#   VENT_ALL  - both sdk and applications (default).
#
# usage:
#   cmake --build --preset linux-debug                    # builds VENT_ALL (default)
#   cmake --build --preset linux-debug --target VENT_SDK
#   cmake --build --preset linux-debug --target VENT_APPS
#   cmake --build --preset linux-debug --target VENT_ALL

# VENT_SDK: builds the sdk.
add_custom_target(VENT_SDK
    DEPENDS vent_headers vent_templates vent_cmake_config copy_compile_commands
    COMMENT "building vent sdk"
)

# add all module targets as dependencies to VENT_SDK.
foreach(MODULE_NAME IN LISTS VENT_ALL_MODULES)
    set(MODULE_TARGET "vent_${MODULE_NAME}")
    if(TARGET ${MODULE_TARGET})
        add_dependencies(VENT_SDK ${MODULE_TARGET})
    endif()
endforeach()

# add all plugin targets as dependencies to VENT_SDK.
foreach(PLUGIN_NAME IN LISTS VENT_ALL_PLUGINS)
    set(PLUGIN_TARGET "vent_${PLUGIN_NAME}")
    if(TARGET ${PLUGIN_TARGET})
        add_dependencies(VENT_SDK ${PLUGIN_TARGET})
    endif()
endforeach()

# VENT_APPS: builds all applications, but not the sdk.
# note: applications depend on VENT_SDK implicitly via their library dependencies.
add_custom_target(VENT_APPS
    DEPENDS copy_compile_commands
    COMMENT "building vent applications"
)

# add all app targets as dependencies to VENT_APPS.
# note: app targets are named after their directory
foreach(APP_TARGET minimal)
    if(TARGET ${APP_TARGET})
        add_dependencies(VENT_APPS ${APP_TARGET})
    endif()
endforeach()

# VENT_ALL: builds everything. this is the default target.
add_custom_target(VENT_ALL ALL
    DEPENDS VENT_SDK VENT_APPS
    COMMENT "building vent sdk and applications"
)

# --- configuration summary ---

vent_log("configuration complete.")
vent_log("  sdk: ${VENT_SDK_DIR}")