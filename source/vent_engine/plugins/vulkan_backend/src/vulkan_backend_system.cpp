// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

// --- vent includes ---

#include <vulkan_backend_system.hpp>
#include <vulkan_pipeline.hpp>

#include <_vent/accessors.hpp>
#include <_vent/job/ic_job.hpp>
#include <_vent/renderer/uniform_buffer.hpp>

#include <core/system/registration.hpp>

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
#include <thread>
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

    create_global_uniforms();
    _depth_format = find_depth_format();

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

    // destroy meshes
    {
        std::lock_guard mesh_lock(_mesh_mutex);
        for (auto& [handle, data] : _meshes) {
            if (data.index_buffer) {
                vmaDestroyBuffer(
                    _allocator, data.index_buffer, data.index_allocation);
            }
            if (data.buffer) {
                vmaDestroyBuffer(_allocator, data.buffer, data.allocation);
            }
        }
        _meshes.clear();
    }

    // destroy textures. the raii view/sampler members clean themselves up, but
    // the VkImage + VmaAllocation are raw and must be freed through vma before
    // the allocator is destroyed below. normally the frontend releases every
    // texture during its own shutdown, but draining here defensively prevents a
    // leak (and a use-after-free of the allocator) if any texture survived.
    {
        std::lock_guard texture_lock(_texture_mutex);
        for (auto& [handle, tex] : _textures) {
            tex.view.clear();
            tex.sampler.clear();
            if (tex.image) {
                vmaDestroyImage(_allocator, tex.image, tex.allocation);
            }
        }
        _textures.clear();
    }

    destroy_global_uniforms();

    if (_allocator) {
        vmaDestroyAllocator(_allocator);
        _allocator = nullptr;
    }

    // raii handles cleanup automatically in reverse declaration order.
    // explicit cleanup only needed for non-raii resources.

    log()->info("vulkan", "vulkan backend shut down.");
}

auto vulkan_backend_system::wait_for_idle() -> void {
    if (*_device)
        _device.waitIdle();
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
        feature_chain = {{.features = {.samplerAnisotropy = VK_TRUE}},
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

    // --- initialize vma ---

    VmaVulkanFunctions vulkan_functions = {};
    vulkan_functions.vkGetInstanceProcAddr =
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
    vulkan_functions.vkGetDeviceProcAddr =
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice         = *_physical_device;
    allocator_info.device                 = *_device;
    allocator_info.instance               = *_instance;
    allocator_info.vulkanApiVersion       = VK_API_VERSION_1_4;
    allocator_info.pVulkanFunctions       = &vulkan_functions;

    if (vmaCreateAllocator(&allocator_info, &_allocator) != VK_SUCCESS) {
        log()->error("vulkan", "failed to create vma allocator.");
        return false;
    }

    // create transfer command pool
    vk::CommandPoolCreateInfo pool_info {
        .flags            = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = _graphics_queue_family,
    };
    try {
        _transfer_command_pool = vk::raii::CommandPool(_device, pool_info);
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to create transfer command pool: {}", err.what());
        return false;
    }

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
                                                       _allocator,
                                                       _depth_format,
                                                       _graphics_queue_family,
                                                       surface_present_family,
                                                       MAX_FRAMES_IN_FLIGHT);
    } catch (const std::exception& e) {
        log()->error("vulkan",
                     "exception during vulkan_swapchain creation: {}.",
                     e.what());
        return false;
    } catch (...) {
        log()->error("vulkan",
                     "unknown exception during vulkan_swapchain creation.");
        return false;
    }

    log()->info("vulkan",
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
                             std::move(surface_present_queue),
                             false});
    }

    log()->info(
        "vulkan", "swapchain created for window '{}'.", window->get_title());

    return true;
}

auto vulkan_backend_system::destroy_surface(ic_window* window) -> void {
    if (!window) {
        return;
    }
    log()->trace("vulkan",
                 "marking surface for destruction for window '{}'",
                 (void*) window);

    // rather than destroying the surface immediately, we mark it for
    // destruction. this avoids race conditions where the render thread is
    // actively recording command buffers for this swapchain while the main
    // thread destroys the window. it will be safely destroyed at the beginning
    // of the next frame.
    std::unique_lock lock(_mutex);
    for (auto& s : _surfaces) {
        if (s.window == window) {
            s.marked_for_destruction = true;
            return;
        }
    }
}

auto vulkan_backend_system::begin_frame(ic_window* window) -> bool {
    std::unique_lock lock(_mutex);

    // clean up any surfaces marked for destruction.
    // this runs on the main thread, naturally synchronizing with end_frame
    // and avoiding threading issues with vkQueueSubmit vs vkWaitForFences.
    for (auto it = _surfaces.begin(); it != _surfaces.end();) {
        if (it->marked_for_destruction) {
            it->swapchain->wait_for_fences();
            log()->info("vulkan",
                        "destroying surface for window handle '{}'.",
                        (void*) it->window);
            it = _surfaces.erase(it);
        } else {
            ++it;
        }
    }

    // loop over remaining active surfaces to find the one requesting the frame.
    for (auto& s : _surfaces) {
        if (s.window == window) {
            // acquiring an image might fail (e.g. window is minimized).
            // if it succeeds, this swapchain becomes the actively rendering one.
            bool success = s.swapchain->begin_frame();
            if (success) {
                _active_swapchain = s.swapchain.get();
                _active_window    = s.window;

                // since begin_frame() just returned true, it means it
                // successfully waited for the fence of this current frame
                // index. this guarantees the gpu is 100% finished with it, so
                // we can safely reset all our thread-local command pools that
                // were used the last time this frame index was rendered for
                // this window!
                reset_thread_contexts(s.window,
                                      s.swapchain->get_current_frame_index());
            }
            return success;
        }
    }
    return false;
}

auto vulkan_backend_system::end_frame(ic_window* window) -> void {
    // wait for all draw calls (and background tasks) to finish generating
    // the secondary command buffers, execute them, and present the final image.
    std::shared_lock lock(_mutex);
    for (auto& s : _surfaces) {
        if (s.window == window) {
            s.swapchain->end_frame(_graphics_queue, s.present_queue);
            _active_swapchain = nullptr;
            return;
        }
    }
}

auto vulkan_backend_system::find_supported_format(
    const std::vector<vk::Format>& candidates,
    vk::ImageTiling                tiling,
    vk::FormatFeatureFlags         features) const -> vk::Format {
    for (vk::Format format : candidates) {
        vk::FormatProperties props =
            _physical_device.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal &&
                   (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    log()->error("vulkan", "failed to find supported format!");
    return vk::Format::eUndefined;
}

auto vulkan_backend_system::find_depth_format() const -> vk::Format {
    return find_supported_format(
        {vk::Format::eD32Sfloat,
         vk::Format::eD32SfloatS8Uint,
         vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

auto vulkan_backend_system::create_graphics_pipeline(pipeline_handle handle,
                                                     const pipeline_desc& desc)
    -> void {
    if (handle == INVALID_PIPELINE_HANDLE)
        return;

    if (_surfaces.empty() || !_surfaces[0].swapchain) {
        log()->error(
            "vulkan",
            "cannot create pipeline: no swapchain available for format/extent");
        return;
    }

    vk::Format   format = _surfaces[0].swapchain->get_image_format();
    vk::Extent2D extent = _surfaces[0].swapchain->get_extent();

    _pipelines[handle] =
        std::make_unique<vulkan_pipeline>(_device,
                                          get_global_descriptor_set_layout(),
                                          desc,
                                          format,
                                          _depth_format,
                                          extent);
}

auto vulkan_backend_system::destroy_graphics_pipeline(pipeline_handle handle)
    -> void {
    if (handle == INVALID_PIPELINE_HANDLE)
        return;
    _pipelines.erase(handle);
}

auto vulkan_backend_system::create_global_uniforms() -> void {
    // 1. descriptor set layout
    vk::DescriptorSetLayoutBinding ubo_binding {
        .binding            = 0,
        .descriptorType     = vk::DescriptorType::eUniformBuffer,
        .descriptorCount    = 1,
        .stageFlags         = vk::ShaderStageFlagBits::eVertex,
        .pImmutableSamplers = nullptr,
    };

    vk::DescriptorSetLayoutBinding sampler_binding {
        .binding            = 1,
        .descriptorType     = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount    = 1,
        .stageFlags         = vk::ShaderStageFlagBits::eFragment,
        .pImmutableSamplers = nullptr,
    };

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {ubo_binding,
                                                              sampler_binding};

    vk::DescriptorSetLayoutCreateInfo layout_info {
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings    = bindings.data(),
    };
    _global_descriptor_set_layout =
        vk::raii::DescriptorSetLayout(_device, layout_info);

    // 2. create buffers.
    // one uniform buffer + descriptor set per frame in flight (not per swapchain
    // image): the cpu writes the ring slot for the frame it is preparing, and
    // the matching fence proves the gpu is done with that slot before we reuse
    // it. sized off the single MAX_FRAMES_IN_FLIGHT knob, no longer a local "3".
    const u32    max_frames  = MAX_FRAMES_IN_FLIGHT;
    VkDeviceSize buffer_size = sizeof(uniform_buffer_object);

    _global_uniform_buffers.resize(max_frames);
    _global_uniform_allocations.resize(max_frames);
    _global_uniform_mapped.resize(max_frames);

    for (size_t i = 0; i < max_frames; i++) {
        VkBufferCreateInfo buffer_info {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = buffer_size,
            .usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VmaAllocationCreateInfo alloc_info {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo alloc_result_info;

        vmaCreateBuffer(_allocator,
                        &buffer_info,
                        &alloc_info,
                        &_global_uniform_buffers[i],
                        &_global_uniform_allocations[i],
                        &alloc_result_info);

        _global_uniform_mapped[i] = alloc_result_info.pMappedData;
    }

    // 3. descriptor pool
    std::array<vk::DescriptorPoolSize, 2> pool_sizes = {
        vk::DescriptorPoolSize {vk::DescriptorType::eUniformBuffer, max_frames},
        vk::DescriptorPoolSize {vk::DescriptorType::eCombinedImageSampler,
                                max_frames}};

    vk::DescriptorPoolCreateInfo pool_info {
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = max_frames,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes    = pool_sizes.data(),
    };
    _descriptor_pool = vk::raii::DescriptorPool(_device, pool_info);

    // 4. allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(
        max_frames, *_global_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo alloc_info {
        .descriptorPool     = *_descriptor_pool,
        .descriptorSetCount = max_frames,
        .pSetLayouts        = layouts.data(),
    };

    _global_descriptor_sets = vk::raii::DescriptorSets(_device, alloc_info);

    // 5. write descriptor sets
    for (size_t i = 0; i < max_frames; i++) {
        vk::DescriptorBufferInfo buffer_info {
            .buffer = _global_uniform_buffers[i],
            .offset = 0,
            .range  = sizeof(uniform_buffer_object),
        };

        vk::WriteDescriptorSet descriptor_write {
            .dstSet          = *_global_descriptor_sets[i],
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &buffer_info,
        };

        _device.updateDescriptorSets(descriptor_write, nullptr);
    }
}

auto vulkan_backend_system::destroy_global_uniforms() -> void {
    _global_descriptor_sets.clear();
    _descriptor_pool              = nullptr;
    _global_descriptor_set_layout = nullptr;

    for (size_t i = 0; i < _global_uniform_buffers.size(); i++) {
        vmaDestroyBuffer(_allocator,
                         _global_uniform_buffers[i],
                         _global_uniform_allocations[i]);
    }
    _global_uniform_buffers.clear();
    _global_uniform_allocations.clear();
    _global_uniform_mapped.clear();
}

auto vulkan_backend_system::update_global_uniforms(
    const uniform_buffer_object& ubo) -> void {
    if (!_active_swapchain || _global_uniform_mapped.empty())
        return;

    u32 frame = _active_swapchain->get_current_frame_index();
    if (frame < _global_uniform_mapped.size() &&
        _global_uniform_mapped[frame]) {
        std::memcpy(_global_uniform_mapped[frame], &ubo, sizeof(ubo));
    }
}

auto vulkan_backend_system::get_thread_context() -> thread_command_context {
    std::lock_guard lock(_context_mutex);

    // check if we have any idle contexts sitting in the pool.
    // this avoids expensive vulkan object creation during rendering.
    if (!_available_contexts.empty()) {
        auto ctx = std::move(_available_contexts.back());
        _available_contexts.pop_back();
        ctx.used_buffers = 0;
        return ctx;
    }

    // if the pool is empty, we must create a brand new context.
    // the command pool is created with the transient flag because we will be
    // recording and resetting it continuously every frame.
    vk::CommandPoolCreateInfo pool_info {
        .flags            = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = _graphics_queue_family,
    };

    thread_command_context ctx;
    ctx.pool         = vk::raii::CommandPool(_device, pool_info);
    ctx.used_buffers = 0;
    return ctx;
}

auto vulkan_backend_system::return_thread_context(thread_command_context&& ctx,
                                                  ic_window* window,
                                                  u32 frame_index) -> void {
    std::lock_guard lock(_context_mutex);
    // when a worker thread finishes its chunk of rendering, it gives its
    // context back to the backend. we store it in a 'pending' list tied
    // directly to this specific window and its specific frame in flight. we
    // cannot put it directly back into _available_contexts because the gpu is
    // currently executing the commands inside this pool!
    _pending_contexts[frame_index][window].push_back(std::move(ctx));
}

auto vulkan_backend_system::reset_thread_contexts(ic_window* window,
                                                  u32 frame_index) -> void {
    std::lock_guard lock(_context_mutex);

    // this function is called only when we have successfully acquired an image
    // from a swapchain and successfully waited for its fence.
    // at this exact moment, we guarantee the gpu has finished executing all
    // command buffers associated with this window's specific frame index.
    // therefore, it is finally safe to reset these pools and put them back
    // into the general pool for other threads to use.
    for (auto& ctx : _pending_contexts[frame_index][window]) {
        _device.getDispatcher()->vkResetCommandPool(
            static_cast<VkDevice>(*_device),
            static_cast<VkCommandPool>(*ctx.pool),
            0);
        _available_contexts.push_back(std::move(ctx));
    }

    // clear just the contexts for this specific window.
    // we do not clear the whole map, because other windows might still have
    // pending command buffers running on the gpu for this frame index.
    _pending_contexts[frame_index][window].clear();
}

auto vulkan_backend_system::record_command_chunk(
    std::span<const render_packet> chunk) -> void* {

    if (!_active_swapchain || chunk.empty()) {
        return nullptr;
    }

    u32        current_frame  = _active_swapchain->get_current_frame_index();
    ic_window* current_window = _active_window;

    auto                     ctx = get_thread_context();
    vk::raii::CommandBuffer* cmd = nullptr;

    if (ctx.used_buffers < ctx.buffers.size()) {
        cmd = &ctx.buffers[ctx.used_buffers++];
    } else {
        vk::CommandBufferAllocateInfo alloc_info {
            .commandPool        = *ctx.pool,
            .level              = vk::CommandBufferLevel::eSecondary,
            .commandBufferCount = 1,
        };
        ctx.buffers.push_back(
            std::move(vk::raii::CommandBuffers(_device, alloc_info).front()));
        cmd = &ctx.buffers[ctx.used_buffers++];
    }

    vk::Format format = _active_swapchain->get_image_format();
    vk::CommandBufferInheritanceRenderingInfo inheritance_rendering {
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &format,
        .depthAttachmentFormat   = _depth_format,
        .rasterizationSamples    = vk::SampleCountFlagBits::e1,
    };
    vk::CommandBufferInheritanceInfo inheritance {
        .pNext = &inheritance_rendering,
    };
    vk::CommandBufferBeginInfo begin_info {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                 vk::CommandBufferUsageFlagBits::eRenderPassContinue,
        .pInheritanceInfo = &inheritance,
    };

    cmd->begin(begin_info);

    // note: pipeline / descriptor / push-constant binding happens per packet
    // below, keyed off packet.pipeline. we do not pre-bind an "active" pipeline
    // here — with sorted packets a future optimization can bind only on change,
    // but the state must be re-established at the start of every secondary
    // buffer regardless (inherited state does not carry pipeline bindings).

    vk::Extent2D extent = _active_swapchain->get_extent();
    vk::Viewport viewport {.x        = 0.0f,
                           .y        = 0.0f,
                           .width    = static_cast<float>(extent.width),
                           .height   = static_cast<float>(extent.height),
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f};
    cmd->setViewport(0, viewport);

    vk::Rect2D scissor {.offset = {0, 0}, .extent = extent};
    cmd->setScissor(0, scissor);

    for (const auto& packet : chunk) {
        // resolve the pipeline once. a stale handle here would otherwise throw
        // std::out_of_range from _pipelines.at() on this worker thread, which
        // submit_internal captures into the task and rethrows on the main thread
        // mid-frame where nothing catches it -> std::terminate. skip the packet
        // and move on instead. (pipelines are immutable between begin_frame and
        // end_frame, so reading _pipelines without a lock is safe here.)
        auto pipe_it = _pipelines.find(packet.pipeline);
        if (pipe_it == _pipelines.end() || !pipe_it->second) {
            continue;
        }
        vulkan_pipeline* pipeline = pipe_it->second.get();

        vulkan_mesh_data data;
        {
            std::lock_guard lock(_mesh_mutex);
            auto            it = _meshes.find(packet.mesh);
            if (it != _meshes.end()) {
                data = it->second;
            }
        }

        if (data.buffer) {
            vk::DeviceSize offset = 0;
            cmd->bindVertexBuffers(0, {data.buffer}, {offset});

            cmd->bindPipeline(vk::PipelineBindPoint::eGraphics,
                              pipeline->get_pipeline());
            cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                    pipeline->get_pipeline_layout(),
                                    0,
                                    {*_global_descriptor_sets[current_frame]},
                                    {});
            cmd->pushConstants<math::mat4>(pipeline->get_pipeline_layout(),
                                           vk::ShaderStageFlagBits::eVertex,
                                           0,
                                           packet.transform);

            if (data.index_buffer) {
                cmd->bindIndexBuffer(
                    data.index_buffer, 0, vk::IndexType::eUint32);
                cmd->drawIndexed(data.index_count, 1, 0, 0, 0);
            } else {
                cmd->draw(data.vertex_count, 1, 0, 0);
            }
        }
    }

    cmd->end();

    vk::CommandBuffer raw_cmd = **cmd;

    return_thread_context(std::move(ctx), current_window, current_frame);

    return reinterpret_cast<void*>(static_cast<VkCommandBuffer>(raw_cmd));
}

auto vulkan_backend_system::execute_recorded_commands(
    std::span<void* const> command_lists) -> void {

    if (!_active_swapchain || command_lists.empty()) {
        return;
    }

    std::vector<vk::CommandBuffer> secondary_cmds;
    secondary_cmds.reserve(command_lists.size());

    for (void* handle : command_lists) {
        if (handle) {
            secondary_cmds.push_back(reinterpret_cast<VkCommandBuffer>(handle));
        }
    }

    if (!secondary_cmds.empty()) {
        _active_swapchain->get_command_buffer().executeCommands(secondary_cmds);
    }
}

auto vulkan_backend_system::create_texture(texture_handle      handle,
                                           const texture_desc& desc) -> void {
    if (desc.pixels.empty() || handle == INVALID_TEXTURE_HANDLE)
        return;

    log()->trace("vulkan", "creating texture {}x{}", desc.width, desc.height);

    VkDeviceSize image_size = desc.width * desc.height * 4;

    // 1. staging buffer
    VkBufferCreateInfo staging_buffer_info = {};
    staging_buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_buffer_info.size        = image_size;
    staging_buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc_info = {};
    staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer          staging_buffer;
    VmaAllocation     staging_allocation;
    VmaAllocationInfo staging_alloc_result;

    if (vmaCreateBuffer(_allocator,
                        &staging_buffer_info,
                        &staging_alloc_info,
                        &staging_buffer,
                        &staging_allocation,
                        &staging_alloc_result) != VK_SUCCESS) {
        log()->error("vulkan", "failed to create texture staging buffer.");
        return;
    }

    std::memcpy(staging_alloc_result.pMappedData,
                desc.pixels.data(),
                static_cast<size_t>(image_size));

    // 2. image
    VkImageCreateInfo image_info = {};
    image_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType         = VK_IMAGE_TYPE_2D;
    image_info.extent.width      = desc.width;
    image_info.extent.height     = desc.height;
    image_info.extent.depth      = 1;
    image_info.mipLevels         = 1;
    image_info.arrayLayers       = 1;
    image_info.format            = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples     = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo image_alloc_info = {};
    image_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    vulkan_texture tex_data;
    if (vmaCreateImage(_allocator,
                       &image_info,
                       &image_alloc_info,
                       &tex_data.image,
                       &tex_data.allocation,
                       nullptr) != VK_SUCCESS) {
        log()->error("vulkan", "failed to create texture image.");
        vmaDestroyBuffer(_allocator, staging_buffer, staging_allocation);
        return;
    }

    // 3. transfer commands
    {
        std::lock_guard lock(_mesh_mutex);

        vk::CommandBufferAllocateInfo alloc_info {
            .commandPool        = *_transfer_command_pool,
            .level              = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };

        vk::raii::CommandBuffers cmd_buffers(_device, alloc_info);
        vk::raii::CommandBuffer& cmd = cmd_buffers[0];

        vk::CommandBufferBeginInfo begin_info {
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        cmd.begin(begin_info);

        // transition undefined -> transfer_dst
        vk::ImageMemoryBarrier barrier {
            .srcAccessMask       = vk::AccessFlagBits::eNone,
            .dstAccessMask       = vk::AccessFlagBits::eTransferWrite,
            .oldLayout           = vk::ImageLayout::eUndefined,
            .newLayout           = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = tex_data.image,
            .subresourceRange    = {
                   .aspectMask     = vk::ImageAspectFlagBits::eColor,
                   .baseMipLevel   = 0,
                   .levelCount     = 1,
                   .baseArrayLayer = 0,
                   .layerCount     = 1,
            }};

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eTransfer,
                            vk::DependencyFlags(),
                            nullptr,
                            nullptr,
                            barrier);

        // copy buffer to image
        vk::BufferImageCopy region {
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask     = vk::ImageAspectFlagBits::eColor,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
            .imageOffset = {0, 0, 0},
            .imageExtent = {desc.width, desc.height, 1}};

        cmd.copyBufferToImage(staging_buffer,
                              tex_data.image,
                              vk::ImageLayout::eTransferDstOptimal,
                              region);

        // transition transfer_dst -> shader_read_only
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            vk::DependencyFlags(),
                            nullptr,
                            nullptr,
                            barrier);

        cmd.end();

        vk::SubmitInfo submit_info {};
        submit_info.setCommandBuffers(*cmd);
        _graphics_queue.submit(submit_info, nullptr);
        _graphics_queue.waitIdle();
    }

    vmaDestroyBuffer(_allocator, staging_buffer, staging_allocation);

    // 4. view and sampler
    vk::ImageViewCreateInfo view_info {
        .image            = tex_data.image,
        .viewType         = vk::ImageViewType::e2D,
        .format           = vk::Format::eR8G8B8A8Srgb,
        .subresourceRange = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        }};
    tex_data.view = vk::raii::ImageView(_device, view_info);

    vk::SamplerCreateInfo sampler_info {
        .magFilter               = vk::Filter::eLinear,
        .minFilter               = vk::Filter::eLinear,
        .mipmapMode              = vk::SamplerMipmapMode::eLinear,
        .addressModeU            = vk::SamplerAddressMode::eRepeat,
        .addressModeV            = vk::SamplerAddressMode::eRepeat,
        .addressModeW            = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = 16.0f,
        .compareEnable           = VK_FALSE,
        .compareOp               = vk::CompareOp::eAlways,
        .borderColor             = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = VK_FALSE,
    };
    tex_data.sampler = vk::raii::Sampler(_device, sampler_info);

    // 5. save
    {
        std::lock_guard lock(_texture_mutex);
        _textures[handle] = std::move(tex_data);
    }

    // 6. update global descriptors
    for (size_t i = 0; i < _global_descriptor_sets.size(); i++) {
        vk::DescriptorImageInfo image_info_write {
            .sampler     = *_textures[handle].sampler,
            .imageView   = *_textures[handle].view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };

        vk::WriteDescriptorSet descriptor_write {
            .dstSet          = *_global_descriptor_sets[i],
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo      = &image_info_write,
        };

        _device.updateDescriptorSets(descriptor_write, nullptr);
    }
}

auto vulkan_backend_system::destroy_texture(texture_handle handle) -> void {
    // wait for the gpu to finish using the texture before destroying it.
    // ideally, we'd use a deferred deletion queue instead of blocking the cpu.
    if (*_device)
        _device.waitIdle();

    std::lock_guard lock(_texture_mutex);
    auto            it = _textures.find(handle);
    if (it != _textures.end()) {
        it->second.view.clear();
        it->second.sampler.clear();
        vmaDestroyImage(_allocator, it->second.image, it->second.allocation);
        _textures.erase(it);
    }
}
auto vulkan_backend_system::create_mesh(mesh_handle               handle,
                                        std::span<const vertex>   vertices,
                                        std::span<const uint32_t> indices)
    -> void {
    log()->trace("vulkan",
                 "creating mesh ({} vertices, {} indices)",
                 vertices.size(),
                 indices.size());
    if (vertices.empty() || handle == INVALID_MESH_HANDLE)
        return;

    size_t vertex_size = sizeof(vertex) * vertices.size();
    size_t index_size  = sizeof(uint32_t) * indices.size();
    size_t buffer_size = vertex_size + index_size;

    // vulkan requires creating two buffers to upload data to the gpu optimally.
    // 1. a "staging" buffer that is visible to the cpu so we can write to it.
    // 2. a "device local" buffer that is blazing fast for the gpu to read from.

    // create staging buffer (cpu visible).
    VkBufferCreateInfo staging_buffer_info = {};
    staging_buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    staging_buffer_info.size        = buffer_size;
    staging_buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc_info = {};
    staging_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer          staging_buffer;
    VmaAllocation     staging_allocation;
    VmaAllocationInfo staging_alloc_result;

    if (vmaCreateBuffer(_allocator,
                        &staging_buffer_info,
                        &staging_alloc_info,
                        &staging_buffer,
                        &staging_allocation,
                        &staging_alloc_result) != VK_SUCCESS) {
        log()->error("vulkan", "failed to create staging buffer.");
        return;
    }

    // copy data to staging buffer.
    std::memcpy(staging_alloc_result.pMappedData, vertices.data(), vertex_size);
    if (index_size > 0) {
        std::memcpy(static_cast<char*>(staging_alloc_result.pMappedData) +
                        vertex_size,
                    indices.data(),
                    index_size);
    }

    // create gpu-local vertex buffer.
    VkBufferCreateInfo vertex_buffer_info = {};
    vertex_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertex_buffer_info.size  = vertex_size;
    vertex_buffer_info.usage =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertex_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vertex_alloc_info = {};
    vertex_alloc_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    vulkan_mesh_data mesh_data;
    mesh_data.vertex_count = static_cast<u32>(vertices.size());
    mesh_data.index_count  = static_cast<u32>(indices.size());

    if (vmaCreateBuffer(_allocator,
                        &vertex_buffer_info,
                        &vertex_alloc_info,
                        &mesh_data.buffer,
                        &mesh_data.allocation,
                        nullptr) != VK_SUCCESS) {
        log()->error("vulkan", "failed to create vertex buffer.");
        vmaDestroyBuffer(_allocator, staging_buffer, staging_allocation);
        return;
    }

    // create gpu-local index buffer if indices exist.
    if (index_size > 0) {
        VkBufferCreateInfo index_buffer_info = {};
        index_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        index_buffer_info.size  = index_size;
        index_buffer_info.usage =
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        index_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(
                _allocator,
                &index_buffer_info,
                &vertex_alloc_info,  // same alloc info as vertex buffer
                &mesh_data.index_buffer,
                &mesh_data.index_allocation,
                nullptr) != VK_SUCCESS) {
            log()->error("vulkan", "failed to create index buffer.");
            vmaDestroyBuffer(
                _allocator, mesh_data.buffer, mesh_data.allocation);
            vmaDestroyBuffer(_allocator, staging_buffer, staging_allocation);
            return;
        }
    }

    // copy staging to vertex (and index) buffer using a transient command buffer.
    {
        std::lock_guard lock(_mesh_mutex);  // protect queue submission and
                                            // command pool allocation.

        vk::CommandBufferAllocateInfo alloc_info {
            .commandPool        = *_transfer_command_pool,
            .level              = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };

        vk::raii::CommandBuffers cmd_buffers(_device, alloc_info);
        vk::raii::CommandBuffer& cmd = cmd_buffers[0];

        vk::CommandBufferBeginInfo begin_info {
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
        };
        cmd.begin(begin_info);

        vk::BufferCopy vertex_copy_region {
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = vertex_size,
        };
        cmd.copyBuffer(staging_buffer, mesh_data.buffer, vertex_copy_region);

        if (index_size > 0) {
            vk::BufferCopy index_copy_region {
                .srcOffset = vertex_size,
                .dstOffset = 0,
                .size      = index_size,
            };
            cmd.copyBuffer(
                staging_buffer, mesh_data.index_buffer, index_copy_region);
        }

        cmd.end();

        vk::SubmitInfo submit_info {};
        submit_info.setCommandBuffers(*cmd);
        _graphics_queue.submit(submit_info);
        _graphics_queue.waitIdle();
    }

    // cleanup staging buffer.
    vmaDestroyBuffer(_allocator, staging_buffer, staging_allocation);

    {
        std::lock_guard lock(_mesh_mutex);
        _meshes[handle] = mesh_data;
    }
}

auto vulkan_backend_system::destroy_mesh(mesh_handle handle) -> void {
    if (handle == INVALID_MESH_HANDLE)
        return;

    std::lock_guard lock(_mesh_mutex);
    auto            it = _meshes.find(handle);
    if (it != _meshes.end()) {
        if (it->second.buffer) {
            vmaDestroyBuffer(
                _allocator, it->second.buffer, it->second.allocation);
        }
        if (it->second.index_buffer) {
            vmaDestroyBuffer(_allocator,
                             it->second.index_buffer,
                             it->second.index_allocation);
        }
        _meshes.erase(it);
        log()->trace("vulkan", "destroyed mesh {}", handle);
    }
}

}  // namespace vent

// --- system registration ---
// —————————————————————————————————————————————————————————————————————————————

VENT_REGISTER_SYSTEM(vent::vulkan_backend_system, vent::i_render_backend)