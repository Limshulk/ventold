// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

// --- vent includes ---

#include <vulkan_backend_system.hpp>

#include <_vent/accessors.hpp>

#include <core/system/registration.hpp>
#include <vulkan_pipeline.hpp>
#include <_vent/interfaces/ic_pipeline.hpp>

// --- platform surface headers ---
// needed for platform-specific surface creation.
// VK_USE_PLATFORM_* are defined via cmake compile definitions so they are
// active before any vulkan header is included (including from our own header).

#ifdef VENT_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

// --- vulkan includes ---
// dynamic dispatch manager storage must be defined exactly once in the entire
// program.

#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace vent {

// --- debug messenger callback ---
// —————————————————————————————————————————————————————————————————————————————

namespace {

auto VKAPI_ATTR VKAPI_CALL debug_messenger_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT      severity,
    vk::DebugUtilsMessageTypeFlagsEXT             type,
    const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
    void* /*user_data*/) -> VkBool32 {

    const char* type_str = "general";
    if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) {
        type_str = "validation";
    } else if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) {
        type_str = "performance";
    }

    // route vulkan validation messages to the vent log system.
    if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
        log()->error(
            "vulkan.validation", "[{}] {}", type_str, callback_data->pMessage);
    } else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        log()->warn(
            "vulkan.validation", "[{}] {}", type_str, callback_data->pMessage);
    } else if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
        log()->debug(
            "vulkan.validation", "[{}] {}", type_str, callback_data->pMessage);
    } else {
        log()->trace(
            "vulkan.validation", "[{}] {}", type_str, callback_data->pMessage);
    }

    return VK_FALSE;
}

/// @brief check if validation layers should be enabled.
auto should_enable_validation() -> bool {
    // check environment variable override.
    const char* env = std::getenv("VENT_VULKAN_VALIDATION");
    if (env) {
        return std::strcmp(env, "0") != 0;
    }

    // default: enabled in debug builds.
#ifdef VENT_DEBUG
    return true;
#else
    return false;
#endif
}

}  // namespace

// --- lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

auto vulkan_backend_system::initialize() -> bool {
    log()->info("vulkan", "initializing vulkan backend...");

    if (!create_instance()) {
        log()->error("vulkan", "failed to create vulkan instance");
        return false;
    }

    if (!setup_debug_messenger()) {
        log()->error("vulkan", "failed to set up debug messenger");
        return false;
    }

    if (!pick_physical_device()) {
        log()->error("vulkan", "failed to find a suitable gpu");
        return false;
    }

    if (!create_logical_device()) {
        log()->error("vulkan", "failed to create logical device");
        return false;
    }

    log()->info("vulkan", "vulkan backend initialized.");
    return true;
}

auto vulkan_backend_system::shutdown() -> void {
    log()->info("vulkan", "shutting down vulkan backend...");

    // destroy all surfaces before device/instance go away.
    std::unique_lock lock(_mutex);
    if (*_device) {
        _device.waitIdle();
    }
    _surfaces.clear();

    // raii handles cleanup automatically in reverse declaration order.
    // explicit cleanup only needed for non-raii resources.

    log()->info("vulkan", "vulkan backend shut down.");
}

// --- vulkan initialization ---
// —————————————————————————————————————————————————————————————————————————————

auto vulkan_backend_system::create_instance() -> bool {

    // --- api version ---
    // check supported api version. we want vulkan 1.4.

    auto api_version = _context.enumerateInstanceVersion();
    if (api_version < vk::ApiVersion14) {
        log()->error("vulkan",
                     "vulkan 1.4 not supported (api version: {}.{}.{}",
                     VK_VERSION_MAJOR(api_version),
                     VK_VERSION_MINOR(api_version),
                     VK_VERSION_PATCH(api_version));
        return false;
    }

    // --- application info ---

    constexpr vk::ApplicationInfo app_info {
        .pApplicationName   = "vent.application",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "vent",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = vk::ApiVersion14};

    // ----------

    // --- extensions ---
    // gather required extensions.

    std::vector<const char*> extensions =
        platform()->get_window_system_extensions(renderer_api::vulkan);
    if (extensions.empty()) {
        log()->error("vulkan",
                     "failed to retrieve required window system extensions "
                     "from platform.");
        return false;
    }

    // --- validation layers ---

    _validation_enabled = should_enable_validation();

    std::vector<const char*> layers;

    if (_validation_enabled) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);

        // check that khronos validation layer is available.
        constexpr const char* validation_layer = "VK_LAYER_KHRONOS_validation";

        auto available_layers = _context.enumerateInstanceLayerProperties();
        bool layer_found      = std::ranges::any_of(
            available_layers, [](const vk::LayerProperties& layer) {
                return std::strcmp(layer.layerName, validation_layer) == 0;
            });

        if (layer_found) {
            layers.push_back(validation_layer);
            log()->info("vulkan", "validation layers enabled.");
        } else {
            log()->warn("vulkan",
                        "validation layers requested but "
                        "VK_LAYER_KHRONOS_validation not available.");
            _validation_enabled = false;
        }
    }

    // --- check extension support ---

    auto available_extensions = _context.enumerateInstanceExtensionProperties();
    for (const char* required : extensions) {
        if (std::ranges::none_of(
                available_extensions, [required](auto const& available_ext) {
                    return std::strcmp(available_ext.extensionName, required) ==
                           0;
                })) {
            log()->error("vulkan",
                         "required extension not supported: {}.",
                         std::string(required));
            return false;
        }
    }

    // --- instance creation ---
    // create the vulkan instance with the specified application info and
    // extensions.

    vk::InstanceCreateInfo create_info {
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = static_cast<u32>(layers.size()),
        .ppEnabledLayerNames     = layers.data(),
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

auto vulkan_backend_system::setup_debug_messenger() -> bool {
    if (!_validation_enabled) {
        log()->trace("vulkan",
                     "validation layers disabled, skipping debug "
                     "messenger setup.");
        return true;
    }

    vk::DebugUtilsMessengerCreateInfoEXT create_info {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debug_messenger_callback,
    };

    try {
        _debug_messenger =
            vk::raii::DebugUtilsMessengerEXT(_instance, create_info);
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create debug messenger: {}", err.what());
        return false;
    }

    log()->trace("vulkan", "debug messenger created.");
    return true;
}

auto vulkan_backend_system::pick_physical_device() -> bool {
    auto devices = _instance.enumeratePhysicalDevices();

    if (devices.empty()) {
        log()->error("vulkan", "no gpus with vulkan support found.");
        return false;
    }

    log()->trace("vulkan", "found {} gpu(s):", devices.size());

    // score each device and pick the best one.
    i32                      best_score           = -1;
    vk::raii::PhysicalDevice best_device          = nullptr;
    u32                      best_graphics_family = 0;

    for (auto& device : devices) {
        auto properties = device.getProperties();
        auto features   = device.getFeatures();

        // --- check queue family support ---
        auto queue_families  = device.getQueueFamilyProperties();
        i32  graphics_family = -1;

        for (u32 i = 0; i < static_cast<u32>(queue_families.size()); ++i) {
            if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                graphics_family = static_cast<i32>(i);
                break;
            }
        }

        if (graphics_family < 0) {
            log()->trace("vulkan",
                         "  - {} : skipped (no graphics queue)",
                         properties.deviceName.data());
            continue;
        }

        // --- score the device ---
        i32 score = 0;

        switch (properties.deviceType) {
            case vk::PhysicalDeviceType::eDiscreteGpu: score += 1000; break;
            case vk::PhysicalDeviceType::eIntegratedGpu: score += 100; break;
            case vk::PhysicalDeviceType::eVirtualGpu: score += 50; break;
            default: break;
        }

        // bonus for tessellation support.
        if (features.tessellationShader) {
            score += 10;
        }

        // vram size gives a rough indicator of gpu capability.
        auto memory_properties = device.getMemoryProperties();
        for (u32 i = 0; i < memory_properties.memoryHeapCount; ++i) {
            if (memory_properties.memoryHeaps[i].flags &
                vk::MemoryHeapFlagBits::eDeviceLocal) {
                // add 1 point per 256 MB of vram.
                score +=
                    static_cast<i32>(memory_properties.memoryHeaps[i].size /
                                     (256ull * 1024 * 1024));
            }
        }

        log()->trace("vulkan",
                     "  - {} : type={}, score={}.",
                     properties.deviceName.data(),
                     vk::to_string(properties.deviceType),
                     score);

        if (score > best_score) {
            best_score           = score;
            best_device          = std::move(device);
            best_graphics_family = static_cast<u32>(graphics_family);
        }
    }

    if (best_score < 0 || !(*best_device)) {
        log()->error("vulkan", "no suitable gpu found.");
        return false;
    }

    _physical_device       = std::move(best_device);
    _graphics_queue_family = best_graphics_family;
    // present queue family will be determined per-surface.

    auto props = _physical_device.getProperties();
    log()->info("vulkan",
                "selected gpu: {} (driver: {}.{}.{}).",
                props.deviceName.data(),
                VK_VERSION_MAJOR(props.driverVersion),
                VK_VERSION_MINOR(props.driverVersion),
                VK_VERSION_PATCH(props.driverVersion));

    return true;
}

auto vulkan_backend_system::create_logical_device() -> bool {
    // --- queue creation ---
    // create unique queue families to avoid duplicating queue create infos.
    std::unordered_set<u32> unique_families = {
        _graphics_queue_family,
    };

    float                                  queue_priority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(unique_families.size());

    for (u32 family : unique_families) {
        queue_create_infos.push_back(vk::DeviceQueueCreateInfo {
            .queueFamilyIndex = family,
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    // --- required device extensions ---
    std::vector<const char*> device_extensions = {
        vk::KHRSwapchainExtensionName,
    };

    // check that required extensions are supported.
    auto available_extensions =
        _physical_device.enumerateDeviceExtensionProperties();
    for (const char* required : device_extensions) {
        bool found = std::ranges::any_of(
            available_extensions,
            [required](const vk::ExtensionProperties& ext) {
                return std::strcmp(ext.extensionName, required) == 0;
            });
        if (!found) {
            log()->error("vulkan",
                         "required device extension not supported: {}.",
                         required);
            return false;
        }
    }

    // --- device features ---
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        feature_chain = {{},
                         {.shaderDrawParameters = true},
                         {.synchronization2 = true, .dynamicRendering = true},
                         {.extendedDynamicState = true}};

    // --- create logical device ---
    vk::DeviceCreateInfo create_info {
        .pNext = &feature_chain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount    = static_cast<u32>(queue_create_infos.size()),
        .pQueueCreateInfos       = queue_create_infos.data(),
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<u32>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures        = nullptr};

    try {
        _device = vk::raii::Device(_physical_device, create_info);
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create logical device: {}", err.what());
        return false;
    }

    // retrieve queue handles.
    _graphics_queue = vk::raii::Queue(_device, _graphics_queue_family, 0);

    log()->trace("vulkan",
                 "logical device created (graphics queue family: {}).",
                 _graphics_queue_family);

    return true;
}

// --- surface management ---
// —————————————————————————————————————————————————————————————————————————————

// todo: should we implement glfwCreateWindowSurface to avoid platform-dependent
// todo: code in here?
auto vulkan_backend_system::create_surface_for_window(ic_window* window)
    -> vk::raii::SurfaceKHR {

    void* native_handle  = window->get_native_handle();
    void* native_display = window->get_native_display();

    if (!native_handle) {
        log()->error("vulkan", "window has no native handle.");
        return nullptr;
    }

    try {
        switch (platform()->get_platform_type()) {
#ifdef VENT_WINDOWS
            case platform_type::win32: {
                vk::Win32SurfaceCreateInfoKHR create_info {
                    .hinstance = static_cast<HINSTANCE>(native_display),
                    .hwnd      = static_cast<HWND>(native_handle)};
                return vk::raii::SurfaceKHR(_instance, create_info);
            }
#endif

#ifdef VENT_LINUX
            case platform_type::x11: {
                vk::XlibSurfaceCreateInfoKHR create_info {
                    .dpy    = static_cast<Display*>(native_display),
                    .window = reinterpret_cast<::Window>(native_handle)};
                return vk::raii::SurfaceKHR(_instance, create_info);
            }
            case platform_type::wayland: {
                vk::WaylandSurfaceCreateInfoKHR create_info {
                    .display = static_cast<wl_display*>(native_display),
                    .surface = static_cast<wl_surface*>(native_handle)};
                return vk::raii::SurfaceKHR(_instance, create_info);
            }
#endif

            case platform_type::cocoa: {
#ifdef VENT_MACOS
                // todo: metal surface creation
                // for now, fuck you
#endif
                log()->error("vulkan",
                             "macOS surface creation not yet implemented.");
                return nullptr;
            }

            default:
                log()->error("vulkan",
                             "unsupported platform type for surface creation.");
                return nullptr;
        }
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create vulkan surface: {}", err.what());
        return nullptr;
    }
}

auto vulkan_backend_system::create_surface(ic_window* window) -> bool {
    if (!window) {
        log()->error("vulkan", "cannot create surface for null window.");
        return false;
    }

    {
        std::shared_lock lock(_mutex);
        // check if surface already exists for this window.
        for (const auto& entry : _surfaces) {
            if (entry.window == window) {
                log()->warn("vulkan",
                            "surface already exists for this window.");
                return true;
            }
        }
    }
    auto surface = create_surface_for_window(window);
    if (!(*surface)) {
        return false;
    }

    // verify that the physical device supports presentation to this surface.
    auto queue_families         = _physical_device.getQueueFamilyProperties();
    u32  surface_present_family = 0;
    bool present_supported      = false;

    for (u32 i = 0; i < static_cast<u32>(queue_families.size()); ++i) {
        if (_physical_device.getSurfaceSupportKHR(i, *surface)) {
            // prefer the graphics queue family for presentation.
            if (i == _graphics_queue_family) {
                surface_present_family = i;
                present_supported      = true;
                break;
            }
        }
    }

    if (!present_supported) {
        log()->error("vulkan",
                     "graphics queue family does not support presentation to "
                     "this surface.");
        return false;
    }

    // --- swapchain ---

    std::unique_ptr<vulkan_swapchain> swapchain;
    try {
        swapchain = std::make_unique<vulkan_swapchain>(_device,
                                                       _physical_device,
                                                       std::move(surface),
                                                       window,
                                                       _graphics_queue_family,
                                                       surface_present_family,
                                                       2);
    } catch (const std::exception& e) {
        log()->error("vulkan",
                     "Exception during vulkan_swapchain creation: {}",
                     e.what());
        return false;
    } catch (...) {
        log()->error("vulkan",
                     "Unknown exception during vulkan_swapchain creation");
        return false;
    }

    log()->trace("vulkan",
                 "surface created for window '{}' (present queue family: {}).",
                 window->get_title(),
                 surface_present_family);

    vk::raii::Queue surface_present_queue =
        _device.getQueue(surface_present_family, 0);

    {
        std::unique_lock lock(_mutex);
        // double-check to prevent concurrent creation
        for (const auto& entry : _surfaces) {
            if (entry.window == window) {
                return true;
            }
        }
        _surfaces.push_back({window,
                             std::move(swapchain),
                             surface_present_family,
                             std::move(surface_present_queue)});
    }

    log()->trace(
        "vulkan", "swapchain created for window '{}'.", window->get_title());

    return true;
}

auto vulkan_backend_system::destroy_surface(ic_window* window) -> void {
    if (!window) {
        return;
    }

    std::unique_lock lock(_mutex);
    for (auto it = _surfaces.begin(); it != _surfaces.end(); ++it) {
        if (it->window == window) {
            // wait for swapchain fences before tearing down.
            it->swapchain->wait_for_fences();

            log()->trace("vulkan",
                         "destroying surface for window '{}'.",
                         window->get_title());

            _surfaces.erase(it);
            return;
        }
    }
}

auto vulkan_backend_system::set_frames_in_flight(ic_window* window, u32 count)
    -> void {
    std::unique_lock lock(_mutex);
    for (auto& s : _surfaces) {
        if (s.window == window) {
            s.swapchain->set_frames_in_flight(count);
            return;
        }
    }
}

auto vulkan_backend_system::begin_frame(ic_window* window) -> bool {
    std::shared_lock lock(_mutex);
    for (auto& s : _surfaces) {
        if (s.window == window) {
            return s.swapchain->begin_frame();
        }
    }
    return false;
}

auto vulkan_backend_system::end_frame(ic_window* window) -> void {
    std::shared_lock lock(_mutex);
    for (auto& s : _surfaces) {
        if (s.window == window) {
            s.swapchain->end_frame(_graphics_queue, s.present_queue);
            return;
        }
    }
}

auto vulkan_backend_system::create_graphics_pipeline(const pipeline_desc& desc) -> std::unique_ptr<ic_pipeline> {
    if (_surfaces.empty()) {
        log()->error("vulkan", "cannot create pipeline: no surfaces exist to determine image format.");
        return nullptr;
    }
    
    // For now, grab the format and extent from the first swapchain.
    // In a multi-window setup, we might need a more robust way to match pipelines to swapchains.
    vk::Format   format = _surfaces[0].swapchain->get_image_format();
    vk::Extent2D extent = _surfaces[0].swapchain->get_extent();

    return std::make_unique<vulkan_pipeline>(_device, desc, format, extent);
}

}  // namespace vent

// --- system registratiob ---
// —————————————————————————————————————————————————————————————————————————————

VENT_REGISTER_SYSTEM(vent::vulkan_backend_system, vent::i_render_backend)