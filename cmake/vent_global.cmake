# vent build system.
# ——————————————————————
# provides platform detection, path setup, and utility functions.
#
# this file sets up:
#   - platform detection (linux, windows, macos).
#   - compiler detection (gcc, clang, msvc).
#   - build type handling (debug, release, ship).
#   - sdk output paths.
#   - c++ compile definitions.
#   - utility functions.

include_guard(GLOBAL)

# --- platform detection ---
# ——————————————————————————————————————————————————————————————————————————————
# detects the target platform and sets:
#   VENT_PLATFORM          - "linux", "windows", or "macos".
#   VENT_PLATFORM_LINUX    - TRUE if linux.
#   VENT_PLATFORM_WINDOWS  - TRUE if windows.
#   VENT_PLATFORM_MACOS    - TRUE if macos.
# note: these are cmake-internal. source code defines are set down below.

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VENT_PLATFORM "linux")
    set(VENT_PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(VENT_PLATFORM "windows")
    set(VENT_PLATFORM_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VENT_PLATFORM "macos")
    set(VENT_PLATFORM_MACOS TRUE)
else()
    message(FATAL_ERROR "[vent] unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# --- compiler detection ---
# ——————————————————————————————————————————————————————————————————————————————
# detects the compiler and sets:
#   VENT_COMPILER       - "gcc", "clang", or "msvc".
#   VENT_COMPILER_GCC   - TRUE if gcc.
#   VENT_COMPILER_CLANG - TRUE if clang.
#   VENT_COMPILER_MSVC  - TRUE if msvc.

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(VENT_COMPILER "gcc")
    set(VENT_COMPILER_GCC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(VENT_COMPILER "clang")
    set(VENT_COMPILER_CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(VENT_COMPILER "msvc")
    set(VENT_COMPILER_MSVC TRUE)
else()
    message(WARNING "[vent] unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
    set(VENT_COMPILER "unknown")
endif()

# --- build type ---
# ——————————————————————————————————————————————————————————————————————————————
# handles debug, release, and ship build types.
# ship is a custom type for production with development tools stripped.

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "build type" FORCE)
endif()

set(VENT_BUILD_TYPE "${CMAKE_BUILD_TYPE}")

# register ship as a valid configuration type.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Ship" CACHE STRING "" FORCE)

# ship uses release optimization flags.
set(CMAKE_CXX_FLAGS_SHIP "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_SHIP "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_SHIP "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

mark_as_advanced(CMAKE_CXX_FLAGS_SHIP CMAKE_EXE_LINKER_FLAGS_SHIP CMAKE_SHARED_LINKER_FLAGS_SHIP)

# --- sdk output ---
# ——————————————————————————————————————————————————————————————————————————————
# the sdk is built to: build/<platform>/<compiler>/<config>/sdk/
#
# structure:
#   sdk/
#   ├── include/_vent/     public headers.
#   ├── lib/               static libraries (one per module).
#   ├── plugins/           shared plugins.
#   └── templates/         launcher template.

# source directories.
set(VENT_ROOT_DIR "${CMAKE_SOURCE_DIR}")
set(VENT_ENGINE_DIR "${VENT_ROOT_DIR}/source/vent_engine")
set(VENT_SDK_SOURCE_DIR "${VENT_ENGINE_DIR}/_vent")
set(VENT_MODULES_DIR "${VENT_ENGINE_DIR}/modules")
set(VENT_PLUGINS_DIR "${VENT_ENGINE_DIR}/plugins")
set(VENT_TEMPLATES_DIR "${VENT_ENGINE_DIR}/templates")

# sdk output directory.
set(VENT_SDK_DIR "${CMAKE_SOURCE_DIR}/build/${VENT_PLATFORM}/${VENT_COMPILER}/${VENT_BUILD_TYPE}/sdk")
set(VENT_SDK_INCLUDE_DIR "${VENT_SDK_DIR}/include")
set(VENT_SDK_LIB_DIR "${VENT_SDK_DIR}/lib")
set(VENT_SDK_PLUGINS_DIR "${VENT_SDK_DIR}/plugins")
set(VENT_SDK_TEMPLATES_DIR "${VENT_SDK_DIR}/templates")

# --- glboal compile definitions ---
# ——————————————————————————————————————————————————————————————————————————————
# these are added to all targets:
#   VENT_DEBUG=1   / VENT_RELEASE=1 / VENT_SHIP=1
#   VENT_LINUX=1   / VENT_WINDOWS=1 / VENT_MACOS=1

# build type.
if(VENT_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(VENT_DEBUG=1)
elseif(VENT_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(VENT_RELEASE=1)
elseif(VENT_BUILD_TYPE STREQUAL "Ship")
    add_compile_definitions(VENT_SHIP=1 NDEBUG=1)
endif()

# platform.
if(VENT_PLATFORM_LINUX)
    add_compile_definitions(VENT_LINUX=1)
elseif(VENT_PLATFORM_WINDOWS)
    add_compile_definitions(VENT_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(VENT_PLATFORM_MACOS)
    add_compile_definitions(VENT_MACOS=1)
endif()

# --- utility functions ---
# ——————————————————————————————————————————————————————————————————————————————

# --- vent_log ---

function(vent_log MESSAGE)
    message(STATUS "[vent] ${MESSAGE}")
endfunction()

# --- vent_set_common_properties ---
# applies:
#   - position independent code.
#   - compiler warnings.
#   - symbol visibility (hidden by default).
#   - build-type-specific optimizations.

function(vent_set_common_properties TARGET)
    set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(VENT_COMPILER_GCC OR VENT_COMPILER_CLANG)
        target_compile_options(${TARGET} PRIVATE
            -Wall -Wextra -Wpedantic -Wno-unused-parameter
            -fvisibility=hidden -fvisibility-inlines-hidden
        )

        if(VENT_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${TARGET} PRIVATE -g3 -O0)
        elseif(VENT_BUILD_TYPE STREQUAL "Release")
            target_compile_options(${TARGET} PRIVATE -O2)
        elseif(VENT_BUILD_TYPE STREQUAL "Ship")
            target_compile_options(${TARGET} PRIVATE -O3 -flto)
            target_link_options(${TARGET} PRIVATE -flto)
        endif()

        # on windows with gcc/mingw, link experimental c++ library for std::print support.
        if(VENT_PLATFORM_WINDOWS)
            target_link_libraries(${TARGET} PRIVATE stdc++exp)
        endif()
    elseif(VENT_COMPILER_MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /permissive- /Zc:preprocessor)
    endif()
endfunction()

# --- log configuration ---
# ——————————————————————————————————————————————————————————————————————————————

vent_log("common utilities loaded")
vent_log("  platform: ${VENT_PLATFORM}")
vent_log("  compiler: ${VENT_COMPILER}")
vent_log("  build type: ${VENT_BUILD_TYPE}")