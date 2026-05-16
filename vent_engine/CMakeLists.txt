# vent build system.
# ——————————————————————
# builds the vent sdk: headers, modules, plugins, templates, and cmake config.
#
# the sdk is output to: build/<platform>/<compiler>/<config>/sdk/
#   sdk/
#   ├── include/_vent/     public headers (copied from _vent/).
#   ├── lib/               static libraries (one per module).
#   ├── plugins/           shared plugins.
#   ├── templates/         launcher template.
#   └── cmake/             cmake config for find_package(vent).
#       ├── vent-config.cmake
#       └── vent-targets.cmake

vent_log("configuring vent engine...")

# sdk cmake output directory.
set(VENT_SDK_CMAKE_DIR "${VENT_SDK_DIR}/cmake")

# --- copy headers ---
# copies _vent/ to sdk/include/_vent/.
# this target always runs to ensure headers are up to date.

add_custom_target(vent_headers ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VENT_SDK_INCLUDE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${VENT_SDK_SOURCE_DIR}"
        "${VENT_SDK_INCLUDE_DIR}/_vent"
    COMMENT "copying sdk headers"
)

# --- copy templates ---
# copies templates/ to sdk/templates/.

add_custom_target(vent_templates ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VENT_SDK_TEMPLATES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${VENT_TEMPLATES_DIR}"
        "${VENT_SDK_TEMPLATES_DIR}"
    COMMENT "copying sdk templates"
)

# --- modules ---
# each module becomes a static library in sdk/lib/.
# we track all modules to generate cmake config later.

set(VENT_ALL_MODULES "" CACHE INTERNAL "list of all vent modules")

add_subdirectory(modules)

# --- plugins ---
# each plugin becomes a shared library in sdk/plugins/.

#add_subdirectory(plugins)

# --- generate cmake config ---
# creates vent-config.cmake and vent-targets.cmake in sdk/cmake/.
# clients use find_package(vent) to import the sdk.
# used for standalone builds outside of vent workspace.

# generate imported targets for each module.
set(VENT_IMPORTED_TARGETS "")
foreach(MOD IN LISTS VENT_ALL_MODULES)
    # determine library file name based on platform.
    if(VENT_PLATFORM_WINDOWS)
        set(LIB_NAME "vent_${MOD}.lib")
    else()
        set(LIB_NAME "libvent_${MOD}.a")
    endif()

    string(APPEND VENT_IMPORTED_TARGETS "
# ${MOD} module.
if(NOT TARGET vent::${MOD})
    add_library(vent::${MOD} STATIC IMPORTED)
    set_target_properties(vent::${MOD} PROPERTIES
        IMPORTED_LOCATION \"\${VENT_SDK_LIB_DIR}/${LIB_NAME}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${VENT_SDK_INCLUDE_DIR}\"
    )
endif()
")
endforeach()

# configure the templates.
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/vent-config.cmake.in"
    "${CMAKE_BINARY_DIR}/vent-config.cmake"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/vent-targets.cmake.in"
    "${CMAKE_BINARY_DIR}/vent-targets.cmake"
    @ONLY
)

# copy to sdk directory at build time.
add_custom_target(vent_cmake_config ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VENT_SDK_CMAKE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/vent-config.cmake"
        "${VENT_SDK_CMAKE_DIR}/vent-config.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/vent-targets.cmake"
        "${VENT_SDK_CMAKE_DIR}/vent-targets.cmake"
    DEPENDS "${CMAKE_BINARY_DIR}/vent-config.cmake" "${CMAKE_BINARY_DIR}/vent-targets.cmake"
    COMMENT "copying cmake config to sdk"
)

vent_log("vent engine configured.")
