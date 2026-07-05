# vent build system.
# ——————————————————————
# the platform module provides windowing and event polling via GLFW.
# output: sdk/lib/libvent_platform.a

# --- window: glfw implementation ---
# ——————————————————————————————————————————————————————————————————————————————
# find system glfw 3.4+ or fetch from github.

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY    https://github.com/glfw/glfw.git
    GIT_TAG           7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
    GIT_SHALLOW       TRUE
    FIND_PACKAGE_ARGS 3.4
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

if(TARGET glfw)
    message(STATUS "[vent] using GLFW from: ${glfw_SOURCE_DIR}")
else()
    message(STATUS "[vent] using system-installed GLFW (find_package)")
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
