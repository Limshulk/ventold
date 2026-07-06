# vent build system.
# ——————————————————————
# provides vent_compile_shaders() to compile .slang files to .spv using slang.

include_guard(GLOBAL)
include(vent_fetch_slang)

# usage:
# vent_compile_shaders(
#     TARGET target_name
#     OUTPUT_DIR path/to/output
#     SOURCES
#         file1.slang
#         file2.slang
# )
function(vent_compile_shaders)
    cmake_parse_arguments(
        ARG
        ""
        "TARGET;OUTPUT_DIR"
        "SOURCES"
        ${ARGN}
    )

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "vent_compile_shaders: TARGET is required")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "vent_compile_shaders: OUTPUT_DIR is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "vent_compile_shaders: SOURCES is required")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    set(SPV_FILES "")

    foreach(SOURCE IN LISTS ARG_SOURCES)
        get_filename_component(FILE_NAME "${SOURCE}" NAME)

        # sources may be given relative to the calling CMakeLists (apps) or as
        # absolute paths (engine assets compiled from vent_create_client).
        if(IS_ABSOLUTE "${SOURCE}")
            set(SOURCE_PATH "${SOURCE}")
        else()
            set(SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}")
        endif()

        # append .spv to the original filename to make it clear.
        set(OUTPUT_FILE "${ARG_OUTPUT_DIR}/${FILE_NAME}.spv")

        # compile the shader. we assume [shader("stage")] attributes in the shader code to define the entry points.
        add_custom_command(
            OUTPUT "${OUTPUT_FILE}"
            COMMAND "${VENT_SLANGC_EXECUTABLE}"
            ARGS "${SOURCE_PATH}" -target spirv -o "${OUTPUT_FILE}"
            DEPENDS "${SOURCE_PATH}"
            COMMENT "compiling slang shader: ${FILE_NAME}"
            VERBATIM
        )

        list(APPEND SPV_FILES "${OUTPUT_FILE}")
    endforeach()

    # create a unique custom target for these shaders so the build system tracks them.
    # this is a hash of the output dir to avoid target name collisions if called multiple times for the same target.
    string(MD5 DIR_HASH "${ARG_OUTPUT_DIR}")
    set(SHADER_TARGET "${ARG_TARGET}_shaders_${DIR_HASH}")
    
    if(NOT TARGET ${SHADER_TARGET})
        add_custom_target(${SHADER_TARGET} DEPENDS ${SPV_FILES})
        add_dependencies(${ARG_TARGET} ${SHADER_TARGET})
    else()
        # fallback if somehow called with exact same params.
        add_custom_target(${SHADER_TARGET}_fallback DEPENDS ${SPV_FILES})
        add_dependencies(${ARG_TARGET} ${SHADER_TARGET}_fallback)
    endif()
endfunction()
