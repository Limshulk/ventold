// vulkan_backend plugin.
// vulkan swapchain class implementation.
// ——————————————————————
//
// implements per-window vulkan resources including swapchain, image views,
// command pools and synchronization primitives. uses dynamic rendering.

#include <vulkan_swapchain.hpp>

#include <_vent/accessors.hpp>
#include <iostream>

namespace vent {

vulkan_swapchain::vulkan_swapchain(
    const vk::raii::Device&         device,
    const vk::raii::PhysicalDevice& physical_device,
    vk::raii::SurfaceKHR            surface,
    ic_window*                      window,
    u32                             graphics_family,
    u32                             present_family,
    u32                             frames_in_flight)
    : _device(device),
      _physical_device(physical_device),
      _surface(std::move(surface)),
      _window(window),
      _graphics_family(graphics_family),
      _present_family(present_family),
      _max_frames_in_flight(frames_in_flight) {

    // initialize the swapchain and all required resources.
    if (!create_swapchain() || !create_image_views() ||
        !create_command_pool() || !create_sync_objects()) {
        log()->error("vulkan",
                     "failed to initialize swapchain for window '{}'.",
                     _window->get_title());
    }
}

vulkan_swapchain::~vulkan_swapchain() {
    _device.waitIdle();

    _swapchain.clear();

    _image_views.clear();

    _in_flight_fences.clear();
    _render_finished_semaphores.clear();
    _image_available_semaphores.clear();

    _command_buffers.clear();
    _command_pool.clear();

    _surface.clear();
}

auto vulkan_swapchain::set_frames_in_flight(u32 count) -> void {
    if (count == 0)
        return;
    if (_max_frames_in_flight != count) {
        _max_frames_in_flight = count;
        // trigger recreation
        _needs_recreation = true;
    }
}

auto vulkan_swapchain::wait_for_fences() -> void {
    std::vector<vk::Fence> raw_fences;
    raw_fences.reserve(_in_flight_fences.size());
    for (const auto& fence : _in_flight_fences) {
        raw_fences.push_back(*fence);
    }
    if (!raw_fences.empty()) {
        auto res = _device.waitForFences(
            raw_fences, VK_TRUE, (std::numeric_limits<u64>::max)());
        (void) res;
    }
}

auto vulkan_swapchain::begin_frame() -> bool {
    // handle minimized window.
    if (_window->get_framebuffer_width() == 0 ||
        _window->get_framebuffer_height() == 0) {
        return false;
    }

    // wait for previous frame to complete.
    auto res = _device.waitForFences({*_in_flight_fences[_current_frame]},
                                     VK_TRUE,
                                     (std::numeric_limits<u64>::max)());
    if (res != vk::Result::eSuccess)
        return false;

    // acquire next image with auto-recreation.
    bool acquire_success = false;
    for (int retry = 0; retry < 2; ++retry) {
        if (_needs_recreation) {
            if (!recreate())
                return false;
            _needs_recreation = false;
        }

        try {
            auto acquire_res = _swapchain.acquireNextImage(
                (std::numeric_limits<u64>::max)(),
                *_image_available_semaphores[_current_frame],
                nullptr);

            if (acquire_res.result == vk::Result::eErrorOutOfDateKHR) {
                _needs_recreation = true;
                continue;  // retry.
            } else if (acquire_res.result == vk::Result::eSuboptimalKHR) {
                _needs_recreation =
                    true;  // handle as success but recreate next time.
            }

            _current_image_index = acquire_res.value;
            acquire_success      = true;
            break;

        } catch (const vk::OutOfDateKHRError&) {
            _needs_recreation = true;
            continue;  // retry.
        } catch (const vk::SystemError& err) {
            log()->error(
                "vulkan", "failed to acquire swapchain image: {}", err.what());
            return false;
        }
    }

    if (!acquire_success) {
        return false;
    }

    // reset fence when we are sure to submit.
    _device.resetFences({*_in_flight_fences[_current_frame]});

    // begin recording command buffer
    auto& cmd = _command_buffers[_current_frame];
    cmd.reset({});
    vk::CommandBufferBeginInfo begin_info {};
    cmd.begin(begin_info);

    // transition swapchain image to vk::ImageLayout::eColorAttachmentOptimal.
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = {},
        .dstStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout     = vk::ImageLayout::eUndefined,
        .newLayout     = vk::ImageLayout::eColorAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = _images[_current_image_index],
        .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1}};
    vk::DependencyInfo dependency_info = {.dependencyFlags         = {},
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers    = &barrier};
    cmd.pipelineBarrier2(dependency_info);

    // clear value.
    vk::ClearValue clear_value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);

    // begin rendering.
    vk::RenderingAttachmentInfo rendering_attachment_info {
        .imageView   = *_image_views[_current_image_index],
        .imageLayout = vk::ImageLayout::eAttachmentOptimalKHR,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = clear_value};

    vk::RenderingInfo rendering_info {
        .flags                = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers,
        .renderArea           = {.offset = {0, 0}, .extent = _extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &rendering_attachment_info};

    cmd.setViewport(0,
                    vk::Viewport(0.0f,
                                 0.0f,
                                 static_cast<float>(_extent.width),
                                 static_cast<float>(_extent.height),
                                 0.0f,
                                 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), _extent));

    cmd.beginRendering(rendering_info);

    return true;
}

auto vulkan_swapchain::end_frame(const vk::raii::Queue& graphics_queue,
                                 const vk::raii::Queue& present_queue) -> void {
    auto& cmd = _command_buffers[_current_frame];

    // end rendering.
    cmd.endRendering();

    // transition swapchain image to vk::ImageLayout::ePresentSrcKHR.
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask  = vk::PipelineStageFlagBits2::eBottomOfPipe,
        .dstAccessMask = {},
        .oldLayout     = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout     = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = _images[_current_image_index],
        .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1}};
    vk::DependencyInfo dependency_info = {.dependencyFlags         = {},
                                          .imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers    = &barrier};
    cmd.pipelineBarrier2(dependency_info);

    // end command buffer recording.
    cmd.end();

    // submit command buffer.
    vk::PipelineStageFlags wait_stages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput};

    vk::Semaphore wait_sem = *_image_available_semaphores[_current_frame];
    vk::Semaphore signal_sem =
        *_render_finished_semaphores[_current_image_index];
    vk::SwapchainKHR swapchain_handle = *_swapchain;

    if (FILE* f = fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")) {
        fprintf(
            f,
            "end_frame submit frame=%u image=%u wait_sem=%p signal_sem=%p\n",
            _current_frame,
            _current_image_index,
            (void*) static_cast<VkSemaphore>(wait_sem),
            (void*) static_cast<VkSemaphore>(signal_sem));
        fclose(f);
    }

    vk::SubmitInfo submit_info {};
    submit_info.setWaitSemaphores(wait_sem);
    submit_info.setWaitDstStageMask(wait_stages);
    submit_info.setCommandBuffers(*cmd);
    submit_info.setSignalSemaphores(signal_sem);

    graphics_queue.submit(submit_info, *_in_flight_fences[_current_frame]);

    // present the image.
    vk::PresentInfoKHR present_info {};
    present_info.setWaitSemaphores(signal_sem);
    present_info.setSwapchains(swapchain_handle);
    present_info.setImageIndices(_current_image_index);

    try {
        auto present_res = present_queue.presentKHR(present_info);
        if (FILE* f = fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")) {
            fprintf(f, "present returned %d\n", (int) present_res);
            fclose(f);
        }
        if (present_res == vk::Result::eErrorOutOfDateKHR ||
            present_res == vk::Result::eSuboptimalKHR) {
            _needs_recreation = true;
        }
    } catch (const vk::OutOfDateKHRError&) {
        _needs_recreation = true;
    } catch (const vk::SystemError& err) {
        log()->error(
            "vulkan", "failed to present swapchain image: {}", err.what());
    }

    // advance frame index
    _current_frame = (_current_frame + 1) % _max_frames_in_flight;
}

auto vulkan_swapchain::create_swapchain() -> bool {
    auto capabilities  = _physical_device.getSurfaceCapabilitiesKHR(*_surface);
    auto formats       = _physical_device.getSurfaceFormatsKHR(*_surface);
    auto present_modes = _physical_device.getSurfacePresentModesKHR(*_surface);

    if (formats.empty() || present_modes.empty()) {
        log()->error("vulkan",
                     "no surface formats or present modes available.");
        return false;
    }

    // select surface format.
    // we try to find SRGB color space and 32bit RGBA format. fallback is just
    // the first format in the list, i don't think that will ever happen.
    vk::SurfaceFormatKHR surface_format = formats.front();
    for (const auto& fmt : formats) {
        if (fmt.format == vk::Format::eB8G8R8A8Srgb &&
            fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surface_format = fmt;
            break;
        }
    }

    // select present mode.
    // eFifo is always available (default vsync). eMailbox is basically vsync
    // with unlimited framerate.
    vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
    for (const auto& mode : present_modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            present_mode = mode;
            break;
        }
    }

    // select extent.
    vk::Extent2D extent;
    if (capabilities.currentExtent.width != (std::numeric_limits<u32>::max)()) {
        extent = capabilities.currentExtent;
    } else {
        extent.width  = std::clamp(_window->get_framebuffer_width(),
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(_window->get_framebuffer_height(),
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    // image count
    u32 image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    // pass old swapchain for smoother recreation. we don't pass it directly to
    // create_info as this resulted in random errors (i guess bc of invalid
    // swapchains during recraition or sth).
    vk::raii::SwapchainKHR old_swapchain = std::move(_swapchain);

    vk::SwapchainCreateInfoKHR create_info {
        .surface               = *_surface,
        .minImageCount         = image_count,
        .imageFormat           = surface_format.format,
        .imageColorSpace       = surface_format.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode      = vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .preTransform          = capabilities.currentTransform,
        .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode           = present_mode,
        .clipped               = true,
        .oldSwapchain          = *old_swapchain};

    u32 queue_family_indices[] = {_graphics_family, _present_family};
    if (_graphics_family != _present_family) {
        create_info.imageSharingMode      = vk::SharingMode::eConcurrent;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = queue_family_indices;
    }

    try {
        _swapchain            = vk::raii::SwapchainKHR(_device, create_info);
        _image_format         = surface_format.format;
        _extent               = extent;
        _images               = _swapchain.getImages();
        _max_frames_in_flight = static_cast<u32>(_images.size());
    } catch (const vk::SystemError& err) {
        log()->error("vulkan", "failed to create swapchain: {}", err.what());
        return false;
    }
    // old_swapchain is destroyed here when it goes out of scope.

    return true;
}

auto vulkan_swapchain::create_image_views() -> bool {
    _image_views.clear();
    _image_views.reserve(_images.size());

    for (const auto& image : _images) {
        vk::ImageViewCreateInfo create_info {
            .image            = image,
            .viewType         = vk::ImageViewType::e2D,
            .format           = _image_format,
            .components       = vk::ComponentMapping(),
            .subresourceRange = vk::ImageSubresourceRange {
                .aspectMask     = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1}};
        try {
            _image_views.emplace_back(_device, create_info);
        } catch (const vk::SystemError& err) {
            log()->error(
                "vulkan", "failed to create image views: {}", err.what());
            return false;
        }
    }
    return true;
}

auto vulkan_swapchain::create_command_pool() -> bool {
    vk::CommandPoolCreateInfo pool_info {
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = _graphics_family};
    try {
        _command_pool = vk::raii::CommandPool(_device, pool_info);

        vk::CommandBufferAllocateInfo alloc_info {
            .commandPool        = *_command_pool,
            .level              = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = _max_frames_in_flight};
        _command_buffers = vk::raii::CommandBuffers(_device, alloc_info);
    } catch (const vk::SystemError& err) {
        log()->error("vulkan",
                     "failed to create command pool or buffers: {}",
                     err.what());
        return false;
    }
    return true;
}

auto vulkan_swapchain::create_sync_objects() -> bool {
    vk::SemaphoreCreateInfo semaphore_info {};
    vk::FenceCreateInfo     fence_info {.flags =
                                        vk::FenceCreateFlagBits::eSignaled};

    try {
        for (u32 i = 0; i < _max_frames_in_flight; ++i) {
            _image_available_semaphores.push_back(
                vk::raii::Semaphore(_device, semaphore_info));
            if (FILE* f =
                    fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")) {
                fprintf(f,
                        "created image_sem[%u] = %p\n",
                        i,
                        (void*) static_cast<VkSemaphore>(
                            *_image_available_semaphores.back()));
                fclose(f);
            }
            _in_flight_fences.push_back(vk::raii::Fence(_device, fence_info));
        }
        for (u32 i = 0; i < _images.size(); ++i) {
            _render_finished_semaphores.push_back(
                vk::raii::Semaphore(_device, semaphore_info));
            if (FILE* f =
                    fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")) {
                fprintf(f,
                        "created signal_sem[%u] = %p\n",
                        i,
                        (void*) static_cast<VkSemaphore>(
                            *_render_finished_semaphores.back()));
                fclose(f);
            }
        }
    } catch (const vk::SystemError& err) {
        log()->error("vulkan", "failed to create sync objects: {}", err.what());
        return false;
    }
    return true;
}

auto vulkan_swapchain::recreate() -> bool {
    log()->trace("vulkan",
                 "swapchain recreation for window '{}' started.",
                 _window->get_title());

    if (FILE* f = fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")) {
        fprintf(f, "recreate() called for swapchain\n");
        fclose(f);
    }
    log()->trace("vulkan", "log written.", _window->get_title());
    _device.waitIdle();
    if (_window->get_framebuffer_width() == 0 ||
        _window->get_framebuffer_height() == 0) {
        return false;  // wait until not minimized.
    }

    // wait for all fences of this swapchain.
    wait_for_fences();

    // cleanup non-swapchain resources, swapchain is handled via .oldSwapchain.
    _image_views.clear();

    if (!create_swapchain() || !create_image_views()) {
        log()->error("vulkan", "failed to recreate swapchain!");
        return false;
    }

    log()->trace(
        "vulkan", "swapchain recreated for window '{}'.", _window->get_title());
    return true;
}

}  // namespace vent
