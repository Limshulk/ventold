// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

// --- vent includes ---

#include <vulkan_backend.hpp>

#include <_vent/accessors.hpp>
#include <vulkan_types.hpp>

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
    // todo: create surface via platform module.
    // _surface = platform()->create_vulkan_surface(_instance);

    if (!pick_physical_device()) {
        log()->error("vulkan", "failed to pick suitable physical device");
        return false;
    }

    if (!create_logical_device()) {
        log()->error("vulkan", "failed to create logical device");
        return false;
    }

    // todo: create swapchain.
    // todo: create command pools and sync objects.

    log()->info("vulkan", "vulkan backend initialized.");
    return true;
}

auto vulkan_backend::shutdown() -> void {
    log()->info("vulkan", "shutting down vulkan backend...");

    // wait for the logical device to be idle before destroying resources.
    if (_device) {
        _device.waitIdle();
    }

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

auto vulkan_backend::pick_physical_device() -> bool {
    log()->trace("vulkan", "picking physical device...");

    auto physical_devices = vk::raii::PhysicalDevices(_instance);
    if (physical_devices.empty()) {
        log()->error("vulkan", "failed to find GPUs with vulkan support");
        return false;
    }

    // todo: implement logic to find the best suitable device.
    // for now, we just pick the first one.
    _physical_device = std::move(physical_devices.front());

    auto props = _physical_device.getProperties();
    log()->info("vulkan", "selected physical device: {}", props.deviceName.data());

    // todo: find queue families.
    // queue_family_indices indices = find_queue_families(_physical_device);

    return true;
}

auto vulkan_backend::create_logical_device() -> bool {
    log()->trace("vulkan", "creating logical device...");

    // todo: find queue families for the selected physical device.
    // todo: create vk::DeviceQueueCreateInfo structs.
    // todo: specify required device features (e.g., for anisotropy).
    // todo: create the vk::raii::Device.
    // todo: get queue handles from the logical device.
    return false;  // return true on success
}

}  // namespace vent

// --- system registratiob ---
// —————————————————————————————————————————————————————————————————————————————

VENT_REGISTER_SYSTEM(vent::vulkan_backend, vent::i_render_backend)