# vent build system.
# ——————————————————————
# the platform module provides windowing and event polling via GLFW.
# output: sdk/lib/libvent_platform.a

# --- window: glfw implementation ---
# ——————————————————————————————————————————————————————————————————————————————
# find system glfw 3.4+ or fetch from github.

find_package(glfw3 3.4 QUIET)

if(NOT glfw3_FOUND)
    message(STATUS "[vent] glfw 3.4 not found, fetching from github...")

    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )

    # configure glfw build options.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(glfw)
endif()

# --- create platform module ---
# ——————————————————————————————————————————————————————————————————————————————

vent_create_module(
    NAME platform
    SOURCES
        src/window.cpp
        src/platform_system.cpp
    INTERNAL_DEPS
        core
    EXTERNAL_LIBS
        glfw
)
