# Vulkan Backend Modernization & Bug Fixes

Comprehensive refactor of the Vulkan rendering backend to fix critical bugs, modernize to Vulkan 1.3 dynamic rendering, and improve synchronization.

## User Review Required

> [!IMPORTANT]
> **V8 (Dynamic Rendering) is a significant architectural change.** It removes `VkRenderPass` and `VkFramebuffer` entirely, replacing them with `vkCmdBeginRendering`/`vkCmdEndRendering`. This simplifies the code but changes the rendering API semantics. Future code that depends on render passes (e.g., subpasses, input attachments) would need a different approach.

> [!IMPORTANT]
> **V9 (vkDeviceWaitIdle reduction)** — My recommendation is to only use per-swapchain fence-based synchronization instead of device-wide stalls. `destroy_surface()` will wait only for that surface's in-flight fences, not the entire device. `cleanup_swapchain()` will do the same. `shutdown()` keeps `vkDeviceWaitIdle` since it's tearing everything down.

> [!IMPORTANT]
> **#8 (type_info hack)** — The current `type_info::name()` string comparison in `system_registry` is a workaround for cross-DLL RTTI on Windows. The proper fix is a **string-based interface ID system** where each interface declares a static constexpr ID string. This is a larger architectural change affecting all `ic_*`/`i_*` interfaces. I recommend deferring this to a separate task since it touches the entire SDK surface. Should I include it in this plan or handle it separately?

## Open Questions

> [!IMPORTANT]
> **V7 (Dynamic Viewport/Scissor):** There are no graphics pipelines in the codebase yet. I'll add the dynamic state to the swapchain's `begin_frame()` via `vkCmdSetViewport`/`vkCmdSetScissor`, so when pipelines are eventually created with `VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR`, they'll work automatically. Is this the right approach, or should I skip V7 until pipelines exist?

---

## Proposed Changes

### Phase 1: Critical Synchronization Fixes

#### [MODIFY] [vulkan_swapchain.hpp](file:///c:/dev/vent/vent_engine/plugins/vulkan_backend/private/vulkan_swapchain.hpp)

- **V1:** Change `_render_finished_semaphores` to be per-frame (same count as `_max_frames_in_flight`), not per-image.
- **V8:** Remove `_render_pass` and `_framebuffers` members entirely. Remove `create_render_pass()` and `create_framebuffers()` helper declarations.
- Add `_needs_recreation` flag for deferred swapchain recreation.
- Add `_minimized` flag for tracking window minimization state.
- Add helper: `transition_image_layout()` for manual layout transitions (needed by dynamic rendering).
- Add helper: `wait_for_fences()` to wait on per-swapchain fences (for V9).

#### [MODIFY] [vulkan_swapchain.cpp](file:///c:/dev/vent/vent_engine/plugins/vulkan_backend/src/vulkan_swapchain.cpp)

**V1 — Fix semaphore indexing:**
- `create_sync_objects()`: Create `_render_finished_semaphores` with `_max_frames_in_flight` count (not `_images.size()`).
- `end_frame()`: Index `_render_finished_semaphores` by `_current_frame` instead of `_current_image_index`.

**V2 — Add swapchain recreation on OUT_OF_DATE:**
- `begin_frame()`: On `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR`, call `recreate()` and retry acquire once. If still failing, return false.
- `end_frame()`: On present failure with OUT_OF_DATE, set `_needs_recreation = true` so next `begin_frame()` triggers recreation.

**V3 — Fence safety:**
- `begin_frame()`: Only call `resetFences` immediately before the command buffer recording block. Ensure no early return path exists between `resetFences` and the eventual `submit` in `end_frame()`. Currently the code is already correct, but I'll restructure to make the invariant clearer.

**V4 — Use oldSwapchain during recreation:**
- `recreate()`: Pass the old swapchain handle to `create_swapchain()` as a parameter.
- `create_swapchain()`: Accept optional `VkSwapchainKHR oldSwapchain` parameter. Set `.oldSwapchain` in `VkSwapchainCreateInfoKHR`. Destroy old swapchain only AFTER new one is created.

**V5 — Recreate command buffers on frames_in_flight change:**
- `set_frames_in_flight()`: After recreation, also call `create_command_pool()` to reallocate command buffers with the new count. Actually since `recreate()` rebuilds sync objects, we need the command buffer count to match.
- `recreate()`: Always recreate command buffers (call `create_command_pool()`) to ensure they match `_max_frames_in_flight`.

**V6 — Handle minimized windows:**
- `begin_frame()`: Check `_window->get_framebuffer_width() == 0 || _window->get_framebuffer_height() == 0`. If so, return `false` immediately without touching any Vulkan state. Log a trace message.
- `recreate()`: Same zero-extent check before attempting swapchain creation.

**V7 — Dynamic viewport/scissor:**
- `begin_frame()`: After beginning the rendering (dynamic rendering), set viewport and scissor via `vkCmdSetViewport` and `vkCmdSetScissor` using the current swapchain extent.

**V8 — Switch to dynamic rendering:**
- Remove `create_render_pass()` and `create_framebuffers()` entirely.
- Constructor: Remove calls to `create_render_pass()` and `create_framebuffers()`.
- `begin_frame()`: Replace `vkCmdBeginRenderPass` with:
  1. Image layout transition: `VK_IMAGE_LAYOUT_UNDEFINED` → `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` via pipeline barrier.
  2. `vkCmdBeginRendering()` with `VkRenderingInfo` and `VkRenderingAttachmentInfo`.
- `end_frame()`: Replace `vkCmdEndRenderPass` with:
  1. `vkCmdEndRendering()`.
  2. Image layout transition: `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` → `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` via pipeline barrier.
- `cleanup_swapchain()`: Remove framebuffer cleanup. Remove render pass (it's gone).
- `recreate()`: Remove `create_framebuffers()` call.

**V9 — Reduce vkDeviceWaitIdle:**
- `cleanup_swapchain()`: Instead of `_device.waitIdle()`, wait only for this swapchain's in-flight fences.
- `recreate()`: Remove the redundant `_device.waitIdle()` (cleanup_swapchain already waits for fences).

---

### Phase 2: Backend Fixes

#### [MODIFY] [vulkan_backend.hpp](file:///c:/dev/vent/vent_engine/plugins/vulkan_backend/private/vulkan_backend.hpp)

- **#11:** Store `_present_queue_family` per-surface in the `window_surface` struct, not as a global backend member.
- Update `window_surface` struct to include `present_queue_family`.
- Keep a single `_present_queue` per unique queue family (or retrieve on demand).

#### [MODIFY] [vulkan_backend.cpp](file:///c:/dev/vent/vent_engine/plugins/vulkan_backend/src/vulkan_backend.cpp)

**#15 — Include message type in debug callback:**
- Replace `(void) type;` with a prefix string based on the message type (General, Validation, Performance).
- Include this prefix in the log message.

**V10 — Remove geometry shader scoring bonus:**
- Remove the `if (features.geometryShader) score += 10;` block from `pick_physical_device()`.

**V12 — Use RAII surface creation:**
- Replace the raw C API surface creation (`vkCreateWin32SurfaceKHR`, `vkCreateXlibSurfaceKHR`, etc.) with vulkan-hpp RAII constructors:
  - `vk::raii::SurfaceKHR(_instance, vk::Win32SurfaceCreateInfoKHR{...})` 
  - `vk::raii::SurfaceKHR(_instance, vk::XlibSurfaceCreateInfoKHR{...})`
  - `vk::raii::SurfaceKHR(_instance, vk::WaylandSurfaceCreateInfoKHR{...})`
- This eliminates the raw `VkSurfaceKHR` handle and the manual `VkResult` checking.

**#11 — Per-surface present queue family:**
- `create_surface()`: Find the present queue family for THIS surface and store it in the `window_surface` struct.
- Remove `_present_queue_family` from the backend class (keep only `_graphics_queue_family`).
- The initial `_present_queue` at device creation time uses the graphics queue family as default.
- `end_frame()`: Pass the correct per-surface present queue to the swapchain.

**V9 — Reduce vkDeviceWaitIdle in backend:**
- `destroy_surface()`: Instead of `_device.waitIdle()`, wait only for the specific surface's swapchain fences. This requires exposing a `wait_for_fences()` method on `vulkan_swapchain`.

---

### Phase 3: type_info Deep Dive (Discussion)

**#8** — Deferred to a separate task. The current approach works for MSVC (which uses decorated names that are unique). For a proper solution, each interface would need:
```cpp
class ic_renderer {
public:
    static constexpr std::string_view interface_id = "vent.ic_renderer";
};
```
And `system_registry` would use `interface_id` strings instead of `type_index`. This is a cross-cutting change affecting all interfaces and the registration macros.

---

## Verification Plan

### Manual Verification
1. Build in Debug mode and run `minimal` app — should show 3 windows clearing to dark gray
2. Resize windows — swapchain should recreate cleanly (V2, V4)
3. Minimize and restore a window — should not crash (V6)
4. Close individual windows — remaining windows should continue rendering (V9)
5. Run with `VENT_VULKAN_VALIDATION=1` — no validation errors about semaphore misuse (V1)
6. Check log output for message type prefix in validation messages (#15)
7. Verify the delayed 4th window at frame 60 still works correctly

### Automated Tests
- Build must succeed on the current platform (Windows)
```bash
cmake --build build --config Debug
```
