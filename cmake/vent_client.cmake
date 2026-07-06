# vent build system.
# ——————————————————————
# provides vent_create_client() for building complete client applications.
#
# a client application consists of three parts:
#   1. libvent_engine.so   - engine shared library with selected modules.
#   2. libclient_<app>.so  - client code (hot-reloadable).
#   3. <appname>           - launcher executable that loads engine.
#
# the launcher is a small executable that:
#   - loads libvent_engine.so.
#   - calls vent_engine_main() to start the engine.
#   - the engine then loads libclient_<app>.so.
#
# output structure:
#   build/<platform>/<compiler>/<config>/apps/<appname>/
#   ├── <appname>           launcher executable.
#   ├── libvent_engine.so   engine with linked modules.
#   ├── libclient_<app>.so  client code.
#   └── [plugins...]        copied plugins (available for dynamic loading).
#
# plugin loading modes:
#   - PLUGINS: copied to app directory, available for dynamic loading by systems.
#   - STARTUP_PLUGINS: subset of PLUGINS, loaded automatically at engine startup.
#
# usage modes:
#   1. in-workspace: vent_create_client() in vent_apps/ subdirs.
#   2. standalone:   find_package(vent) then vent_create_client() in user project.

include_guard(GLOBAL)

# --- apps output directory ---
# ——————————————————————————————————————————————————————————————————————————————
# apps are output alongside the sdk but in a separate directory.

set(VENT_APPS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/${VENT_PLATFORM}/${VENT_COMPILER}/${VENT_BUILD_TYPE}/apps")

# --- vent_create_client ---
# ——————————————————————————————————————————————————————————————————————————————

# parameters:
#   NAME             - application name.
#   MODULES          - required vent modules to link into engine.
#   OPTIONAL_MODULES - (optional) modules excluded in ship builds.
#   PLUGINS          - (optional) plugins to copy to app directory (dynamic loading).
#   STARTUP_PLUGINS  - (optional) plugins to load at engine startup (must be in PLUGINS).
#   SOURCES          - client application source files.
#   LAUNCHER         - (optional) custom launcher source file.
#
# usage:
#   vent_create_client(
#       NAME name
#       MODULES
#           core
#           job
#           platform
#           renderer
#       OPTIONAL_MODULES
#           imgui_overlay
#           profiler_ui
#       PLUGINS
#           vulkan_backend     # copied, available for dynamic loading.
#       STARTUP_PLUGINS
#           # (empty - renderer loads vulkan_backend dynamically, not at startup).
#       SOURCES
#           src/main.cpp
#   )
#
# creates targets:
#   name                    - launcher executable.
#   vent_engine_name        - engine shared library.
#   client_name             - client shared library (libclient_name.so).

function(vent_create_client)
    # --- parse arguments ---
    # ——————————————————————————————————————————————————————————————————————————
    
    cmake_parse_arguments(
        APP                                                        # prefix.
        ""                                                         # options.
        "NAME;LAUNCHER"                                            # single-value.
        "MODULES;OPTIONAL_MODULES;PLUGINS;STARTUP_PLUGINS;SOURCES" # multi-value.
        ${ARGN}
    )

    # validate required arguments.
    if(NOT APP_NAME)
        message(FATAL_ERROR "vent_create_client: NAME is required")
    endif()
    if(NOT APP_MODULES)
        message(FATAL_ERROR "vent_create_client: MODULES is required (at minimum: core)")
    endif()
    if(NOT "core" IN_LIST APP_MODULES)
        message(FATAL_ERROR "vent_create_client: MODULES must include 'core' module")
    endif()
    if(NOT APP_SOURCES)
        message(FATAL_ERROR "vent_create_client: SOURCES is required")
    endif()

    vent_log("creating client: ${APP_NAME}")

    # target names.
    set(ENGINE_TARGET "vent_engine_${APP_NAME}")
    set(CLIENT_TARGET "client_${APP_NAME}")
    set(LAUNCHER_TARGET "${APP_NAME}")

    # output directory for this app.
    set(APP_OUTPUT_DIR "${VENT_APPS_OUTPUT_DIR}/${APP_NAME}")

    # generated build info header directory.
    set(BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/generated/${APP_NAME}")

    # --- determine which modules to include ---
    # ——————————————————————————————————————————————————————————————————————————
    # in ship builds, optional modules are excluded.

    set(ALL_MODULES ${APP_MODULES})

    if(APP_OPTIONAL_MODULES)
        if(VENT_BUILD_TYPE STREQUAL "Ship")
            vent_log("  ship build: excluding optional modules")
        else()
            list(APPEND ALL_MODULES ${APP_OPTIONAL_MODULES})
        endif()
    endif()

    vent_log("  modules: ${ALL_MODULES}")

    # --- validate module dependencies ---
    # ——————————————————————————————————————————————————————————————————————————
    # check that all transitive dependencies are included in the module list.
    # this prevents runtime errors from missing system dependencies.
    
    set(MISSING_DEPS "")
    foreach(MOD IN LISTS ALL_MODULES)
        # get dependencies for this module (set by vent_create_module).
        set(MOD_DEPS ${VENT_MODULE_${MOD}_DEPS})
        
        foreach(DEP IN LISTS MOD_DEPS)
            # check if dependency is in our module list.
            list(FIND ALL_MODULES ${DEP} DEP_INDEX)
            if(DEP_INDEX EQUAL -1)
                list(APPEND MISSING_DEPS "${MOD} requires ${DEP}")
            endif()
        endforeach()
    endforeach()
    
    if(MISSING_DEPS)
        # format error message.
        string(REPLACE ";" "\n    - " MISSING_DEPS_STR "${MISSING_DEPS}")
        message(FATAL_ERROR 
"vent_create_client: missing module dependencies for '${APP_NAME}'!

  The following module dependencies are not satisfied:
    - ${MISSING_DEPS_STR}

  Add the missing modules to MODULES in vent_create_client() or remove
  the modules that depend on them.
  
  Current modules: ${ALL_MODULES}")
    endif()

    # --- create engine shared library ---
    # ——————————————————————————————————————————————————————————————————————————
    # the engine library links all required static modules into a single
    # shared library. we use --whole-archive to ensure all symbols are
    # exported (important for static initialization and dynamic lookup).

    vent_log("  creating engine: ${ENGINE_TARGET}")

    # create an empty shared library that will link the static modules.
    add_library(${ENGINE_TARGET} SHARED)

    # dummy source to make cmake happy (shared lib needs at least one source).
    # we'll use a generated file.
    set(ENGINE_DUMMY_SOURCE "${CMAKE_BINARY_DIR}/generated/${APP_NAME}/engine_dummy.cpp")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/${APP_NAME}")
    file(WRITE "${ENGINE_DUMMY_SOURCE}" "// generated. links modules into engine.\n")
    target_sources(${ENGINE_TARGET} PRIVATE "${ENGINE_DUMMY_SOURCE}")

    # link static modules with --whole-archive to export all symbols.
    set(ENGINE_WHOLE_ARCHIVE_LIBS "")
    set(ENGINE_NORMAL_LIBS "")

    foreach(MOD IN LISTS ALL_MODULES)
        set(MOD_TARGET "vent_${MOD}")

        if(NOT TARGET ${MOD_TARGET})
            message(FATAL_ERROR "vent_create_client: module '${MOD}' not found")
        endif()

        list(APPEND ENGINE_WHOLE_ARCHIVE_LIBS ${MOD_TARGET})

        # collect any transitive dependencies.
        get_target_property(LIBS ${MOD_TARGET} INTERFACE_LINK_LIBRARIES)
        if(LIBS)
            foreach(LIB IN LISTS LIBS)
                if(NOT LIB MATCHES "^vent_")
                    list(APPEND ENGINE_NORMAL_LIBS ${LIB})
                endif()
            endforeach()
        endif()
    endforeach()

    # link with whole-archive for static libs, then normal libs.
    if(VENT_COMPILER_GCC OR VENT_COMPILER_CLANG)
        target_link_libraries(${ENGINE_TARGET} PRIVATE
            -Wl,--whole-archive
            ${ENGINE_WHOLE_ARCHIVE_LIBS}
            -Wl,--no-whole-archive
            ${ENGINE_NORMAL_LIBS}
        )
    elseif(VENT_COMPILER_MSVC)
        # msvc uses /WHOLEARCHIVE per-lib.
        foreach(LIB IN LISTS ENGINE_WHOLE_ARCHIVE_LIBS)
            target_link_options(${ENGINE_TARGET} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:${LIB}>")
        endforeach()
        target_link_libraries(${ENGINE_TARGET} PRIVATE ${ENGINE_WHOLE_ARCHIVE_LIBS} ${ENGINE_NORMAL_LIBS})
    endif()

    # engine needs sdk headers.
    # for in-workspace builds, use source directly.
    if(DEFINED VENT_ENGINE_DIR AND EXISTS "${VENT_ENGINE_DIR}/_vent")
        target_include_directories(${ENGINE_TARGET} PRIVATE "${VENT_ENGINE_DIR}")
    else()
        target_include_directories(${ENGINE_TARGET} PRIVATE "${VENT_SDK_INCLUDE_DIR}")
    endif()

    # apply common properties.
    vent_set_common_properties(${ENGINE_TARGET})
    target_compile_definitions(${ENGINE_TARGET} PRIVATE VENT_EXPORT)

    set_target_properties(${ENGINE_TARGET} PROPERTIES
        OUTPUT_NAME "vent_engine"
        LIBRARY_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
    )

    # plugins loaded by this client must resolve engine accessors against the
    # app-specific engine dll, not against their own private module copies.
    # this keeps runtime state shared between the engine and dynamically loaded
    # plugins on windows.
    if(APP_PLUGINS)
        foreach(PLUGIN_NAME IN LISTS APP_PLUGINS)
            set(PLUGIN_TARGET "vent_${PLUGIN_NAME}")
            if(TARGET ${PLUGIN_TARGET})
                target_link_libraries(${PLUGIN_TARGET} PRIVATE ${ENGINE_TARGET})
            endif()
        endforeach()
    endif()

    # --- create client shared library ---
    # ——————————————————————————————————————————————————————————————————————————
    # the client library contains the client code. it's a separate shared lib
    # so it can be hot-reloaded during development.

    vent_log("  creating client: ${CLIENT_TARGET}")

    add_library(${CLIENT_TARGET} SHARED ${APP_SOURCES})

    vent_set_common_properties(${CLIENT_TARGET})

    # client needs sdk headers.
    # for in-workspace builds, use source directly.
    if(DEFINED VENT_ENGINE_DIR AND EXISTS "${VENT_ENGINE_DIR}/_vent")
        target_include_directories(${CLIENT_TARGET} PRIVATE "${VENT_ENGINE_DIR}")
    else()
        target_include_directories(${CLIENT_TARGET} PRIVATE "${VENT_SDK_INCLUDE_DIR}")
    endif()

    target_compile_definitions(${CLIENT_TARGET} PRIVATE
        VENT_EXPORT
        VENT_APP_ID="${APP_NAME}"
    )

    # on windows, client needs to link against engine library for symbol resolution.
    if(VENT_PLATFORM_WINDOWS)
        target_link_libraries(${CLIENT_TARGET} PRIVATE ${ENGINE_TARGET})
    endif()

    set_target_properties(${CLIENT_TARGET} PROPERTIES
        OUTPUT_NAME "client_${APP_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
    )

    # --- create launcher ---
    # ——————————————————————————————————————————————————————————————————————————
    # the launcher is a tiny executable that loads the engine library.

    vent_log("  creating launcher: ${LAUNCHER_TARGET}")

    # use custom launcher if provided, otherwise use template.
    if(APP_LAUNCHER)
        set(LAUNCHER_SOURCE "${APP_LAUNCHER}")
    else()
        # for in-workspace builds, use source template directly.
        # for standalone builds, vent-config.cmake sets VENT_SDK_TEMPLATES_DIR.
        if(DEFINED VENT_TEMPLATES_DIR AND EXISTS "${VENT_TEMPLATES_DIR}/launcher.cpp")
            set(LAUNCHER_SOURCE "${VENT_TEMPLATES_DIR}/launcher.cpp")
        else()
            set(LAUNCHER_SOURCE "${VENT_SDK_TEMPLATES_DIR}/launcher.cpp")
        endif()
    endif()

    add_executable(${LAUNCHER_TARGET} "${LAUNCHER_SOURCE}")

    vent_set_common_properties(${LAUNCHER_TARGET})

    # launcher needs sdk headers and generated build info.
    # for in-workspace builds, use source directly.
    if(DEFINED VENT_ENGINE_DIR AND EXISTS "${VENT_ENGINE_DIR}/_vent")
        target_include_directories(${LAUNCHER_TARGET} PRIVATE "${VENT_ENGINE_DIR}")
    else()
        target_include_directories(${LAUNCHER_TARGET} PRIVATE "${VENT_SDK_INCLUDE_DIR}")
    endif()
    target_include_directories(${LAUNCHER_TARGET} PRIVATE "${BUILD_INFO_DIR}")

    target_compile_definitions(${LAUNCHER_TARGET} PRIVATE VENT_APP_ID=${APP_NAME})

    # linux needs dl library for dlopen/dlsym.
    if(VENT_PLATFORM_LINUX)
        target_link_libraries(${LAUNCHER_TARGET} PRIVATE dl)
    endif()

    set_target_properties(${LAUNCHER_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
    )

    # launcher depends on engine and client being built first.
    add_dependencies(${LAUNCHER_TARGET} ${ENGINE_TARGET} ${CLIENT_TARGET} vent_headers vent_templates)

    # --- copy plugins ---
    # ——————————————————————————————————————————————————————————————————————————

    if(APP_PLUGINS)
        foreach(PLG IN LISTS APP_PLUGINS)
            set(PLG_TARGET "vent_${PLG}")

            if(NOT TARGET ${PLG_TARGET})
                message(FATAL_ERROR "vent_create_client: plugin '${PLG}' not found")
            endif()

            set(COPY_TARGET "${APP_NAME}_copy_${PLG}")
            add_custom_target(${COPY_TARGET} ALL
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${PLG_TARGET}>
                    "${APP_OUTPUT_DIR}/"
                DEPENDS ${PLG_TARGET}
                COMMENT "copying plugin ${PLG} to ${APP_NAME} output directory"
            )

            add_dependencies(${LAUNCHER_TARGET} ${COPY_TARGET})
        endforeach()
    endif()

    # startup plugins: verify they are listed in PLUGINS.
    foreach(PLG IN LISTS APP_STARTUP_PLUGINS)
        list(FIND APP_PLUGINS ${PLG} PLG_INDEX)
        if(PLG_INDEX EQUAL -1)
            message(FATAL_ERROR
                    "vent_create_client: STARTUP_PLUGINS entry '${PLG}' not found in PLUGINS list!
                    All startup plugins must first be listed in PLUGINS to be copied to the app directory.")
        endif()
    endforeach()

    # --- generate build info header ---
    # ——————————————————————————————————————————————————————————————————————————
    # this header is generated in the cmake build directory.
    # it contains compile-time info about included modules and startup plugins.

    set(BUILD_INFO_HEADER "${BUILD_INFO_DIR}/${APP_NAME}_build_info.hpp")

    # build the startup plugins list: STARTUP_PLUGINS + client plugin (client last).
    # add prefixes: "vent_" for engine plugins, "client_" for client plugin.
    # note: only STARTUP_PLUGINS are auto-loaded; other PLUGINS are just copied to be available for
    # dynamic loading by systems (renderer loads vulkan_backend).
    set(STARTUP_PLUGINS_LIST "")
    foreach(PLG IN LISTS APP_STARTUP_PLUGINS)
        list(APPEND STARTUP_PLUGINS_LIST "vent_${PLG}")
    endforeach()
    list(APPEND STARTUP_PLUGINS_LIST "client_${APP_NAME}")
    list(LENGTH STARTUP_PLUGINS_LIST STARTUP_PLUGINS_COUNT)

    # convert lists to c++ array initializers.
    string(REPLACE ";" "\", \"" STARTUP_PLUGINS_STR "${STARTUP_PLUGINS_LIST}")
    string(REPLACE ";" "\", \"" OPT_MODULES_STR "${APP_OPTIONAL_MODULES}")

    if(STARTUP_PLUGINS_LIST)
        set(STARTUP_PLUGINS_INIT "\"${STARTUP_PLUGINS_STR}\"")
    else()
        set(STARTUP_PLUGINS_INIT "")
    endif()

    if(APP_OPTIONAL_MODULES)
        set(OPT_MODULES_INIT "\"${OPT_MODULES_STR}\"")
    else()
        set(OPT_MODULES_INIT "")
    endif()

    if(VENT_BUILD_TYPE STREQUAL "Ship")
        set(IS_SHIP "true")
    else()
        set(IS_SHIP "false")
    endif()

    file(MAKE_DIRECTORY "${BUILD_INFO_DIR}")
    file(WRITE "${BUILD_INFO_HEADER}"
"// GENERATED.
// ——————————————————————
// auto-generated by cmake. do not edit.

#pragma once

#include <_vent/vent_sdk.hpp>

namespace vent::generated {

/// @brief startup plugins to load at engine initialization (client plugin last).
/// @note other plugins (PLUGINS without STARTUP_PLUGINS) are available for
/// dynamic loading by systems (e.g., renderer loads vulkan_backend).
inline constexpr const char* plugins[] = {${STARTUP_PLUGINS_INIT}};

/// @brief number of startup plugins to load.
inline constexpr u32 plugin_count = ${STARTUP_PLUGINS_COUNT};

/// @brief optional modules that may not be present.
inline constexpr const char* const* optional_modules = nullptr;

/// @brief true if this is a ship build.
inline constexpr bool is_ship_build = ${IS_SHIP};

/// @brief application identifier.
inline constexpr const char* app_id = \"${APP_NAME}\";

}  // namespace vent::generated
")

    # client includes the generated header.
    target_include_directories(${CLIENT_TARGET} PRIVATE "${BUILD_INFO_DIR}")

    # --- engine assets ---
    # ——————————————————————————————————————————————————————————————————————————
    # every client ships the engine's default assets under engine_assets/.
    # the asset system mounts vent:// -> <exe_dir>/engine_assets at startup,
    # so the renderer's default + error shaders are always resolvable no
    # matter where the app was copied or launched from.
    # note: in-workspace builds only for now — the standalone sdk path
    # (vent-config.cmake.in) still needs the same step once the sdk carries
    # the engine assets.

    if(DEFINED VENT_ENGINE_DIR AND EXISTS "${VENT_ENGINE_DIR}/assets/shaders")
        vent_compile_shaders(
            TARGET ${LAUNCHER_TARGET}
            OUTPUT_DIR "${APP_OUTPUT_DIR}/engine_assets/shaders"
            SOURCES
                "${VENT_ENGINE_DIR}/assets/shaders/default.slang"
                "${VENT_ENGINE_DIR}/assets/shaders/error.slang"
        )
    endif()

    # --- register with vent_apps aggregate target ---
    # ——————————————————————————————————————————————————————————————————————————

    if(TARGET vent_apps)
        add_dependencies(vent_apps ${LAUNCHER_TARGET})
    endif()

    vent_log("  client ${APP_NAME} ready -> ${APP_OUTPUT_DIR}")
endfunction()

