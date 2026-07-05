# vent build system.
# ——————————————————————
# provides vent_create_module() for building engine modules as static libs.
#
# each module becomes a separate static library (.a) in the sdk/lib/ directory.
# when linking to an application, use --whole-archive to ensure static
# initializers (used for system registration) are not stripped.
#
# directory structure for a module:
#   modules/<name>/
#   ├── CMakeLists.txt       calls vent_create_module().
#   ├── public/<name>/       headers visible to other modules.
#   │   └── interfaces/      i_*.hpp internal interfaces.
#   ├── private/             headers private to this module.
#   └── src/                 implementation files.
#
# output:
#   sdk/lib/libvent_<name>.a

include_guard(GLOBAL)

# --- vent_create_module ---
# parameters:
#   NAME          - module name (without "vent_" prefix).
#   SOURCES       - source files to compile.
#   INTERNAL_DEPS - (optional) other vent modules this depends on.
#   EXTERNAL_LIBS - (optional) external libraries to link.
#
# example:
#   vent_create_module(
#       NAME job
#       SOURCES
#           src/job_system.cpp
#       INTERNAL_DEPS
#           core
#   )
#
# creates target: vent_job
# output: sdk/lib/libvent_job.a

function(vent_create_module)
    # --- parse arguments ---
    # ——————————————————————————————————————————————————————————————————————————

    cmake_parse_arguments(
        MOD                                   # prefix.
        ""                                    # options (flags).
        "NAME"                                # single-value args.
        "SOURCES;INTERNAL_DEPS;EXTERNAL_LIBS" # multi-value args.
        ${ARGN}
    )

    if(NOT MOD_NAME)
        message(FATAL_ERROR "vent_create_module: NAME is required")
    endif()
    if(NOT MOD_SOURCES)
        message(FATAL_ERROR "vent_create_module: SOURCES is required")
    endif()

    set(TARGET_NAME "vent_${MOD_NAME}")

    vent_log("creating module: ${TARGET_NAME}")

    # --- create static library target ---
    # ——————————————————————————————————————————————————————————————————————————

    add_library(${TARGET_NAME} STATIC ${MOD_SOURCES})

    # --- set output directory ---
    # ——————————————————————————————————————————————————————————————————————————

    set_target_properties(${TARGET_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${VENT_SDK_LIB_DIR}"
    )

    # --- common properties ---
    # ——————————————————————————————————————————————————————————————————————————

    vent_set_common_properties(${TARGET_NAME})

    # --- include directories ---
    # ——————————————————————————————————————————————————————————————————————————
    # by default, all modules can include:
    #   <_vent/...>     - public sdk headers.
    #   <core/...>      - core module public headers.
    #   <module/...>    - this module's public headers.
    #   <...>           - this module's private headers.

    # sdk headers.
    target_include_directories(${TARGET_NAME} PUBLIC "${VENT_ENGINE_DIR}")

    # third_party headers (available to all modules).
    target_include_directories(${TARGET_NAME} PUBLIC "${VENT_ENGINE_DIR}/third_party")

    # this module's public headers.
    target_include_directories(${TARGET_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/public"
    )

    # this module's private headers.
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/private"
    )

    # core module headers (all modules need these).
    target_include_directories(${TARGET_NAME} PUBLIC
        "${VENT_MODULES_DIR}/core/public"
    )

    # --- internal module dependencies ---
    # ——————————————————————————————————————————————————————————————————————————

    # check if internal dependency 'core' is missing, and add it automatically.
    # (only if we are NOT the core module itself).
    if(NOT MOD_NAME STREQUAL "core")
        if(NOT "core" IN_LIST MOD_INTERNAL_DEPS)
            message(FATAL_ERROR "${TARGET_NAME}: modules must depend on 'core' module")
        endif()
    endif()

    if(MOD_INTERNAL_DEPS)
        foreach(DEP IN LISTS MOD_INTERNAL_DEPS)
            set(DEP_TARGET "vent_${DEP}")

            # add dependency's public headers.
            target_include_directories(${TARGET_NAME} PUBLIC
                "${VENT_MODULES_DIR}/${DEP}/public"
            )

            # link to dependency.
            if(TARGET ${DEP_TARGET})
                target_link_libraries(${TARGET_NAME} PUBLIC ${DEP_TARGET})
            endif()
        endforeach()
    endif()

    # --- external libraries ---
    # ——————————————————————————————————————————————————————————————————————————

    if(MOD_EXTERNAL_LIBS)
        target_link_libraries(${TARGET_NAME} PUBLIC ${MOD_EXTERNAL_LIBS})
    endif()

    vent_log("  -> ${VENT_SDK_LIB_DIR}/lib${TARGET_NAME}.a")

    # --- track module ---
    # ——————————————————————————————————————————————————————————————————————————
    
    set(VENT_ALL_MODULES ${VENT_ALL_MODULES} ${MOD_NAME} CACHE INTERNAL "list of all vent modules")
    
    # store dependencies for compile-time validation in vent_create_client.
    if(MOD_INTERNAL_DEPS)
        set(VENT_MODULE_${MOD_NAME}_DEPS ${MOD_INTERNAL_DEPS} CACHE INTERNAL "dependencies for ${MOD_NAME}")
    else()
        set(VENT_MODULE_${MOD_NAME}_DEPS "" CACHE INTERNAL "dependencies for ${MOD_NAME}")
    endif()
endfunction()

