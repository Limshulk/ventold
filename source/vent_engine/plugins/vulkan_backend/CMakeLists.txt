# vent build system.
# ——————————————————————
# the vulkan_backend plugin provides a vulkan implementation of the rendering backend.
# output: sdk/plugins/libvent_vulkan_backend.so

# --- vulkan package ---
# ——————————————————————————————————————————————————————————————————————————————
# vulkan sdk 1.4+ is required. provides vulkan.hpp and vulkan_raii.hpp.

find_package(Vulkan REQUIRED)

# verify vulkan-hpp is available (comes with vulkan sdk).
if(NOT EXISTS "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.hpp")
    message(FATAL_ERROR "vulkan.hpp not found. ensure vulkan sdk 1.3+ is installed.")
endif()

if(NOT EXISTS "${Vulkan_INCLUDE_DIR}/vulkan/vulkan_raii.hpp")
    message(FATAL_ERROR "vulkan_raii.hpp not found. ensure vulkan sdk 1.3+ is installed.")
endif()

message(STATUS "[vent] vulkan sdk found: ${Vulkan_INCLUDE_DIR}")

# --- create vulkan_backend plugin ---
# ——————————————————————————————————————————————————————————————————————————————

vent_create_plugin(
    NAME vulkan_backend
    SOURCES
        src/vulkan_backend_system.cpp
        src/vulkan_swapchain.cpp
        src/vulkan_pipeline.cpp

    INTERNAL_DEPS
        core
        renderer
        platform
        
    EXTERNAL_LIBS
        Vulkan::Vulkan
)

# --- vulkan-hpp configuration ---
# ——————————————————————————————————————————————————————————————————————————————
# these defines configure vulkan-hpp for modern c++ usage with raii.
# reference: https://github.com/KhronosGroup/Vulkan-Hpp

target_compile_definitions(vent_vulkan_backend PRIVATE
    # use dynamic dispatch loader. this avoids linking issues and allows
    # loading vulkan functions at runtime. required for raii usage.
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    
    # disable legacy struct constructors. use designated initializers instead:
    #   vk::InstanceCreateInfo{ .pApplicationInfo = &appInfo }
    # instead of:
    #   vk::InstanceCreateInfo(flags, &appInfo, ...)
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1

    # enable validation layers in debug builds.
    $<$<CONFIG:Debug>:VENT_DEBUG>

    # platform-specific vulkan surface support.
    # must be defined before vulkan headers are included.
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XLIB_KHR>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_WAYLAND_KHR>
    $<$<PLATFORM_ID:Darwin>:VK_USE_PLATFORM_METAL_EXT>
)