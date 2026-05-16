# vent build system.
# ——————————————————————
# provides vent_create_plugin() for building hot-reloadable plugins.
#
# plugins are shared libraries (.so/.dll) that can be loaded at runtime.
# they are placed in sdk/plugins/ and can be hot-reloaded during development.
#
# directory structure for a plugin:
#   plugins/<name>/
#   ├── CMakeLists.txt       calls vent_create_plugin().
#   ├── public/<name>/       headers visible to engine/other plugins.
#   ├── private/             headers private to this plugin.
#   └── src/                 implementation files.
#
# output:
#   sdk/plugins/libvent_<name>.so

include_guard(GLOBAL)

# --- vent_create_plugin ---
# parameters:
#   NAME          - plugin name (without "vent_" prefix).
#   SOURCES       - source files to compile.
#   INTERNAL_DEPS - (optional) vent modules this depends on.
#   EXTERNAL_LIBS - (optional) external libraries to link.
#
# example:
#   vent_create_plugin(
#       NAME renderer_vulkan
#       SOURCES
#           src/vulkan_renderer.cpp
#       INTERNAL_DEPS
#           core
#       EXTERNAL_LIBS
#           vulkan
#   )
#
# creates target: vent_renderer_vulkan
# output: sdk/plugins/libvent_renderer_vulkan.so

function(vent_create_plugin)
    # --- parse arguments ---
    # ——————————————————————————————————————————————————————————————————————————

    cmake_parse_arguments(
        PLG                                   # prefix.
        ""                                    # options (flags).
        "NAME"                                # single-value args.
        "SOURCES;INTERNAL_DEPS;EXTERNAL_LIBS" # multi-value args.
        ${ARGN}
    )

    if(NOT PLG_NAME)
        message(FATAL_ERROR "vent_create_plugin: NAME is required")
    endif()
    if(NOT PLG_SOURCES)
        message(FATAL_ERROR "vent_create_plugin: SOURCES is required")
    endif()

    set(TARGET_NAME "vent_${PLG_NAME}")

    vent_log("creating plugin: ${TARGET_NAME}")

    # --- create shared library target ---
    # ——————————————————————————————————————————————————————————————————————————

    add_library(${TARGET_NAME} SHARED ${PLG_SOURCES})

    # --- set output directory ---
    # ——————————————————————————————————————————————————————————————————————————

    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${VENT_SDK_PLUGINS_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${VENT_SDK_PLUGINS_DIR}"  # for windows dlls.
    )

    # --- apply common properties ---
    # ——————————————————————————————————————————————————————————————————————————

    vent_set_common_properties(${TARGET_NAME})

    # plugins need VENT_PLUGIN_EXPORT defined when building (for __declspec(dllexport) on windows).
    target_compile_definitions(${TARGET_NAME} PRIVATE VENT_PLUGIN_EXPORT)

    # --- include directories ---
    # ——————————————————————————————————————————————————————————————————————————

    # sdk headers.
    target_include_directories(${TARGET_NAME} PUBLIC "${VENT_ENGINE_DIR}")

    # this plugin's public headers.
    target_include_directories(${TARGET_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/public"
    )

    # this plugin's private headers.
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/private"
    )

    # core module headers.
    target_include_directories(${TARGET_NAME} PUBLIC
        "${VENT_MODULES_DIR}/core/public"
    )

    # --- module dependencies ---
    # ——————————————————————————————————————————————————————————————————————————

    if(NOT "core" IN_LIST PLG_INTERNAL_DEPS)
        message(FATAL_ERROR "${TARGET_NAME}: plugins must depend on 'core' module")
    endif()

    if(PLG_INTERNAL_DEPS)
        foreach(DEP IN LISTS PLG_INTERNAL_DEPS)
            target_include_directories(${TARGET_NAME} PUBLIC
                "${VENT_MODULES_DIR}/${DEP}/public"
            )
        endforeach()
    endif()

    # plugins must resolve engine accessors from the host engine library at
    # runtime. do not statically link core module objects into the plugin,
    # otherwise the plugin gets its own private registry and registration state.
    if(VENT_COMPILER_GCC OR VENT_COMPILER_CLANG)
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--allow-shlib-undefined"
        )
    endif()

    # --- external libraries ---
    # ——————————————————————————————————————————————————————————————————————————

    if(PLG_EXTERNAL_LIBS)
        target_link_libraries(${TARGET_NAME} PRIVATE ${PLG_EXTERNAL_LIBS})
    endif()

    vent_log("  -> ${VENT_SDK_PLUGINS_DIR}/lib${TARGET_NAME}.so")

    # --- track plugin ---
    # ——————————————————————————————————————————————————————————————————————————
    
    set(VENT_ALL_PLUGINS ${VENT_ALL_PLUGINS} ${PLG_NAME} CACHE INTERNAL "list of all vent plugins")
endfunction()

