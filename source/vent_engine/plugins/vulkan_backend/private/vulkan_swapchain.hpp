#pragma once
//
// vulkan_backend plugin.
// swapchain class.
// ——————————————————————
//
// encapsulates all per-window vulkan resources (swapchain, image views,
// command pools, synchronization primitives, AND the per-frame uniform ring).
// uses vulkan 1.3 dynamic rendering (vkCmdBeginRendering/vkCmdEndRendering)
// instead of render passes.
//
// why the uniform ring lives HERE and not on the backend: a resource's
// storage must live at the scope of the fence that protects it. the frame
// uniforms are written by the cpu and read by the gpu, and the only fence
// that proves a slot is free is THIS swapchain's in-flight fence. one
// backend-global ring indexed by per-swapchain frame counters was a gpu race
// the moment two windows had different cameras (review g-1).

#include <_vent/vent_sdk.hpp>
#include <_vent/platform/ic_window.hpp>
#include <_vent/renderer/uniform_buffer.hpp>

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
    /// @param frame_set_layout descriptor set layout for the per-frame
    /// uniforms (set 0), owned by the backend — layouts describe shape, not
    /// ownership, so one global layout serves every window's private sets.
    /// @param frames_in_flight how many frames can be processed concurrently.
    vulkan_swapchain(const vk::raii::Device&              device,
                     const vk::raii::PhysicalDevice&      physical_device,
                     vk::raii::SurfaceKHR                 surface,
                     ic_window*                           window,
                     VmaAllocator                         allocator,
                     vk::Format                           depth_format,
                     u32                                  graphics_family,
                     u32                                  present_family,
                     const vk::raii::DescriptorSetLayout& frame_set_layout,
                     u32                                  frames_in_flight = 2);

    ~vulkan_swapchain();

    /// @brief recreate the swapchain.
    auto recreate() -> bool;

    /// @brief get the number of frames in flight this swapchain is configured
    /// for (a cpu-side constant, not the swapchain image count).
    [[nodiscard]]
    auto get_frames_in_flight() const -> u32 {
        return _max_frames_in_flight;
    }

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

    /// @brief write the per-frame uniform data into the current frame's ring
    /// slot. must only be called between begin_frame and end_frame: the fence
    /// wait in begin_frame is exactly what proves this slot is no longer read
    /// by the gpu.
    auto write_frame_uniforms(const uniform_buffer_object& ubo) -> void;

    /// @brief get the per-frame descriptor set (set 0: camera ubo) for a
    /// frame index. bound by command recording on job workers.
    [[nodiscard]]
    auto get_frame_descriptor_set(u32 frame_index) const -> vk::DescriptorSet {
        return *_frame_descriptor_sets[frame_index];
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

    // --- per-frame uniforms (set 0) ---
    // one persistently mapped uniform buffer + descriptor set per frame in
    // flight, private to this window. the descriptor pool is per-swapchain on
    // purpose: pool lifetime = surface lifetime, so destruction is automatic
    // and no FreeDescriptorSet juggling is needed.
    std::vector<VkBuffer>                _uniform_buffers;
    std::vector<VmaAllocation>           _uniform_allocations;
    std::vector<void*>                   _uniform_mapped;
    vk::raii::DescriptorPool             _descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> _frame_descriptor_sets;

    // --- initialization helpers ---
    auto create_swapchain() -> bool;
    auto create_image_views() -> bool;
    auto create_depth_resources() -> bool;
    auto create_command_pool() -> bool;
    auto create_sync_objects() -> bool;
    auto create_frame_uniforms(
        const vk::raii::DescriptorSetLayout& frame_set_layout) -> bool;
};

}  // namespace vent
