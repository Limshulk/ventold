#pragma once
//
// vulkan_backend plugin.
// swapchain class.
// ——————————————————————
//
// encapsulates all per-window vulkan resources (swapchain, image views,
// command pools, and synchronization primitives). uses vulkan 1.3 dynamic
// rendering (vkCmdBeginRendering/vkCmdEndRendering) instead of render passes.

#include <_vent/vent_sdk.hpp>
#include <_vent/platform/ic_window.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <vma/vk_mem_alloc.h>

#include <vector>

namespace vent {

/// @brief encapsulates all resources required to present to a window.
class vulkan_swapchain final {
public:
    VENT_NO_COPY_MOVE(vulkan_swapchain);

public:
    /// @brief construct a new swapchain for the given window surface.
    /// @param device the logical device.
    /// @param physical_device the physical device.
    /// @param surface the surface to create the swapchain for.
    /// @param window the window associated with the surface.
    /// @param graphics_family the graphics queue family index.
    /// @param present_family the presentation queue family index.
    /// @param frames_in_flight how many frames can be processed concurrently.
    vulkan_swapchain(const vk::raii::Device&         device,
                     const vk::raii::PhysicalDevice& physical_device,
                     vk::raii::SurfaceKHR            surface,
                     ic_window*                      window,
                     VmaAllocator                    allocator,
                     vk::Format                      depth_format,
                     u32                             graphics_family,
                     u32                             present_family,
                     u32                             frames_in_flight = 2);

    ~vulkan_swapchain();

    /// @brief recreate the swapchain.
    auto recreate() -> bool;

    /// @brief get the maximum number of frames in flight.
    [[nodiscard]]
    auto get_frames_in_flight() const -> u32 {
        return _max_frames_in_flight;
    }

    /// @brief change the number of frames in flight at runtime.
    /// this will force a complete swapchain recreation.
    auto set_frames_in_flight(u32 count) -> void;

    /// @brief acquire the next image from the swapchain to begin rendering.
    auto begin_frame() -> bool;

    /// @brief submit recorded commands and present the image to the screen.
    auto end_frame(const vk::raii::Queue& graphics_queue,
                   const vk::raii::Queue& present_queue) -> void;

    /// @brief wait for all in-flight fences to complete.
    /// used by the backend for per-surface synchronization instead of
    /// vkDeviceWaitIdle.
    auto wait_for_fences() -> void;

    [[nodiscard]]
    auto get_image_format() const -> vk::Format {
        return _image_format;
    }
    [[nodiscard]]
    auto get_extent() const -> vk::Extent2D {
        return _extent;
    }

    [[nodiscard]]
    auto get_command_buffer() -> vk::raii::CommandBuffer& {
        return _command_buffers[_current_frame];
    }

    [[nodiscard]]
    auto get_current_frame_index() const -> u32 {
        return _current_frame;
    }

private:
    const vk::raii::Device&         _device;
    const vk::raii::PhysicalDevice& _physical_device;
    vk::raii::SurfaceKHR            _surface;
    ic_window*                      _window;

    u32 _graphics_family;
    u32 _present_family;
    u32 _max_frames_in_flight;
    u32 _current_frame       = 0;
    u32 _current_image_index = 0;

    bool _needs_recreation = false;  ///< deferred recreation flag.

    vk::Format   _image_format;
    vk::Extent2D _extent;

    // --- synchronization ---
    std::vector<vk::raii::Semaphore> _image_available_semaphores;
    std::vector<vk::raii::Semaphore> _render_finished_semaphores;
    std::vector<vk::raii::Fence>     _in_flight_fences;

    // --- command buffers ---
    vk::raii::CommandPool                _command_pool = nullptr;
    std::vector<vk::raii::CommandBuffer> _command_buffers;

    // --- core swapchain ---
    std::vector<vk::Image>           _images;
    std::vector<vk::raii::ImageView> _image_views;
    vk::raii::SwapchainKHR           _swapchain = nullptr;

    // --- depth buffer ---
    VmaAllocator          _allocator;
    vk::Format            _depth_format;
    VkImage               _depth_image      = nullptr;
    VmaAllocation         _depth_allocation = nullptr;
    vk::raii::ImageView   _depth_image_view = nullptr;

    // --- initialization helpers ---
    auto create_swapchain() -> bool;
    auto create_image_views() -> bool;
    auto create_depth_resources() -> bool;
    auto create_command_pool() -> bool;
    auto create_sync_objects() -> bool;
};

}  // namespace vent
