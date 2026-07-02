// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

// --- vent includes ---

#include <vulkan_backend.hpp>

#include <_vent/accessors.hpp>

#include <core/system/registration.hpp>

// --- vulkan includes ---
// dynamic dispatch manager storage must be defined exactly once in the entire
// program.

#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan_raii.hpp>

namespace vent {

auto vulkan_backend::initialize() -> bool {
    log()->info("vulkan", "initializing vulkan backend...");

    _context = vk::raii::Context {};

    if (!create_instance()) {
        log()->error("vulkan", "failed to create vulkan instance");
        return false;
    }

    // todo: create vulkan instance (next tutorial chapter).
    // todo: pick physical device.
    // todo: create logical device and queues.

    log()->info("vulkan", "vulkan backend initialized.");
    return true;
}

auto vulkan_backend::shutdown() -> void {
    log()->info("vulkan", "shutting down vulkan backend...");

    // raii handles cleanup automatically in reverse declaration order.
    // explicit cleanup only needed for non-raii resources.

    log()->info("vulkan", "vulkan backend shut down.");
}

// --- vulkan initialization ---
// —————————————————————————————————————————————————————————————————————————————

auto vulkan_backend::create_instance() -> bool {

    // --- api version ---
    // check supported api version. we need vulkan 1.3 at least.

    auto api_version = _context.enumerateInstanceVersion();
    if (api_version < vk::ApiVersion13) {
        log()->error("vulkan",
                     "vulkan 1.3 not supported (api version: {}.{}.{}",
                     VK_VERSION_MAJOR(api_version),
                     VK_VERSION_MINOR(api_version),
                     VK_VERSION_PATCH(api_version));
        return false;
    }

    // --- application info ---

    constexpr vk::ApplicationInfo app_info {
        .pApplicationName   = "vent",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "vent",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = vk::ApiVersion13};

    // --- extensions ---
    // gather required extensions. we need:
    //  - base surface extension for window surface creation.
    //  - platform-specific surface for the used window manager system.
    //  - debug utils for validation layers.

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };

    // todo: bad practice. we should abstract this. but this way is simpler for
    // todo: now as we have independent platform module and backend plugins.
    switch (platform()->get_platform_type()) {
        case platform_type::x11:
            extensions.push_back("VK_KHR_xlib_surface");
            break;
        case platform_type::wayland:
            extensions.push_back("VK_KHR_wayland_surface");
            break;
        case platform_type::win32:
            extensions.push_back("VK_KHR_win32_surface");
            break;
        case platform_type::cocoa:
            extensions.push_back("VK_EXT_metal_surface");
            break;
        default:
            log()->error("vulkan",
                         "unsupported platform type for vulkan backend");
            return false;
    }

    // check that all required extensions are supported.
    auto available_extensions = _context.enumerateInstanceExtensionProperties();
    for (const char* required : extensions) {
        bool found = std::ranges::any_of(
            available_extensions,
            [required](const vk::ExtensionProperties& ext) {
                return std::strcmp(ext.extensionName, required) == 0;
            });
        if (!found) {
            log()->error(
                "vulkan", "required extension not supported: {}", required);
            return false;
        }
    }

    // --- instance creation ---
    // create the vulkan instance with the specified application info and
    // extensions.

    vk::InstanceCreateInfo create_info {
        .pApplicationInfo        = &app_info,
        .enabledExtensionCount   = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    try {
        _instance = vk::raii::Instance(_context, create_info);
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create vulkan instance: {}", err.what());
        return false;
    }

    log()->trace("vulkan", "vulkan instance created.");
    return true;
}

}  // namespace vent

// --- system registratiob ---
// —————————————————————————————————————————————————————————————————————————————

VENT_REGISTER_SYSTEM(vent::vulkan_backend, vent::i_render_backend)