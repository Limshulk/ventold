#pragma once
//
// vulkan_backend plugin.
// vulkan rendering backend implementation.
// ——————————————————————
//
// implements the vulkan api backend for vent.

#include <vulkan_swapchain.hpp>

#include <_vent/system/system_base.hpp>
#include <_vent/platform/ic_window.hpp>

#include <renderer/interfaces/i_render_backend.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <vma/vk_mem_alloc.h>

#include <mutex>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace vent {

class vulkan_backend_system final
    : public system_base
    , public i_render_backend {
public:
    vulkan_backend_system()           = default;
    ~vulkan_backend_system() override = default;

    VENT_NO_COPY_MOVE(vulkan_backend_system);
public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return i_render_backend::system_name;
    }

    [[nodiscard]]
    auto on_initialization() -> system_initialization_status override {
        return initialize() ? system_initialization_status::success
                            : system_initialization_status::failed;
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    bool _validation_enabled = false;  ///< vulkan validation layers.

    // --- vulkan raii ---
    // —————————————————————————————————————————————————————————————————————————
    // vulkan raii objects. they are declared in initialization order and
    // automatically destroyed in reverse.

    vk::raii::Context  _context {};  ///< vulkan function loader context.
    vk::raii::Instance _instance = nullptr;  ///< vulkan instance.
    vk::raii::DebugUtilsMessengerEXT _debug_messenger =
        nullptr;  ///< validation layer messenger.
    vk::raii::PhysicalDevice _physical_device = nullptr;  ///< selected gpu.
    vk::raii::Device         _device          = nullptr;  ///< logical device.
    vk::raii::Queue          _graphics_queue  = nullptr;  ///< graphics queue.

    u32 _graphics_queue_family = 0;  ///< graphics queue family index.

    vk::raii::CommandPool _transfer_command_pool =
        nullptr;  ///< command pool for transient transfers.

    VmaAllocator _allocator = nullptr;  ///< memory allocator.

    // --- per-window surfaces ---
    // —————————————————————————————————————————————————————————————————————————
    // surfaces are created per-window and stored alongside the window pointer.

    struct window_surface {
        ic_window*                        window = nullptr;
        std::unique_ptr<vulkan_swapchain> swapchain;
        u32                               present_queue_family   = 0;
        vk::raii::Queue                   present_queue          = nullptr;
        bool                              marked_for_destruction = false;
    };
    std::vector<window_surface> _surfaces;
    std::shared_mutex           _mutex;

    vulkan_swapchain* _active_swapchain =
        nullptr;  ///< the swapchain currently rendering a frame.
    ic_window* _active_window =
        nullptr;  ///< the window currently rendering a frame.

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    struct vulkan_mesh_data {
        VkBuffer      buffer       = VK_NULL_HANDLE;
        VmaAllocation allocation   = VK_NULL_HANDLE;
        u32           vertex_count = 0;

        VkBuffer      index_buffer     = VK_NULL_HANDLE;
        VmaAllocation index_allocation = VK_NULL_HANDLE;
        u32           index_count      = 0;
    };

    std::unordered_map<mesh_handle, vulkan_mesh_data> _meshes;
    std::mutex                                        _mesh_mutex;

    class vulkan_pipeline* _active_pipeline = nullptr;
    std::unordered_map<pipeline_handle, std::unique_ptr<class vulkan_pipeline>> _pipelines;

    // --- global uniforms and descriptors ---
    // —————————————————————————————————————————————————————————————————————————
    std::vector<VkBuffer>      _global_uniform_buffers;
    std::vector<VmaAllocation> _global_uniform_allocations;
    std::vector<void*>         _global_uniform_mapped;

    vk::raii::DescriptorSetLayout        _global_descriptor_set_layout = nullptr;
    vk::raii::DescriptorPool             _descriptor_pool = nullptr;
    std::vector<vk::raii::DescriptorSet> _global_descriptor_sets;

public:
    // --- i_render_backend ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief gets the global descriptor set layout for uniforms.
    auto get_global_descriptor_set_layout() const -> const vk::raii::DescriptorSetLayout& {
        return _global_descriptor_set_layout;
    }

    /// @brief gets the name of the graphics api.
    /// @return string view containing "vulkan".
    [[nodiscard]]
    auto get_api_name() const -> std::string_view override {
        return "vulkan";
    }

    /// @brief creates a vulkan surface and swapchain for the given window.
    /// @param window the window to create a surface for.
    /// @return true if successful, false otherwise.
    auto create_surface(ic_window* window) -> bool override;

    /// @brief destroys the vulkan surface and swapchain associated with the
    /// window.
    /// @param window the window whose surface should be destroyed.
    auto destroy_surface(ic_window* window) -> void override;

    /// @brief sets the maximum number of frames in flight for a window's
    /// swapchain.
    /// @param window the window to modify.
    /// @param count the new frame count (e.g. 2 for double buffering).
    auto set_frames_in_flight(ic_window* window, u32 count) -> void override;

    /// @brief begins a new frame for the given window.
    /// @param window the window to begin rendering for.
    /// @return true if the frame successfully began, false if skipped (e.g.
    /// minimized).
    auto begin_frame(ic_window* window) -> bool override;

    /// @brief ends the frame for the given window and presents it to the screen.
    /// @param window the window to end rendering for.
    auto end_frame(ic_window* window) -> void override;

    /// @brief creates a new graphics pipeline from a description.
    /// @param handle the frontend-generated handle.
    /// @param desc the pipeline descriptor.
    auto create_graphics_pipeline(pipeline_handle handle, const pipeline_desc& desc) -> void override;

    /// @brief destroys a graphics pipeline.
    /// @param handle the pipeline handle.
    auto destroy_graphics_pipeline(pipeline_handle handle) -> void override;

    /// @brief binds a pipeline for subsequent draw calls in the current frame.
    /// @param handle the pipeline handle to bind.
    auto bind_pipeline(pipeline_handle handle) -> void override;

    /// @brief update global uniform buffer data.
    /// @param ubo the uniform data to pass to the renderer.
    auto update_global_uniforms(const uniform_buffer_object& ubo) -> void override;

    // --- mesh management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief creates a gpu mesh buffer from host vertex data.
    /// @param handle the frontend-generated handle.
    /// @param vertices the list of vertices to upload.
    /// @param indices an optional list of indices for indexed drawing.
    auto create_mesh(mesh_handle handle,
                     std::span<const vertex>   vertices,
                     std::span<const uint32_t> indices = {}) -> void override;

    /// @brief destroys a mesh.
    /// @param handle the mesh handle.
    auto destroy_mesh(mesh_handle handle) -> void override;

    // --- rendering execution ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief records commands for a list of render packets on a background thread.
    /// @param chunk the chunk of draw commands to execute.
    /// @return an opaque handle to the recorded command list.
    auto record_command_chunk(std::span<const render_packet> chunk)
        -> void* override;

    /// @brief executes the recorded command lists on the primary command buffer.
    /// @param command_lists span of opaque command list handles returned by record_command_chunk.
    auto execute_recorded_commands(std::span<void* const> command_lists)
        -> void override;

private:
    auto create_global_uniforms() -> void;
    auto destroy_global_uniforms() -> void;

    // --- multithreading ---
    struct thread_command_context {
        vk::raii::CommandPool                pool = nullptr;
        std::vector<vk::raii::CommandBuffer> buffers;
        u32                                  used_buffers = 0;
    };
    std::mutex                          _context_mutex;
    std::vector<thread_command_context> _available_contexts;
    std::unordered_map<ic_window*, std::vector<thread_command_context>>
        _pending_contexts[3];  // up to 3 frames in flight per window.

    /// @brief acquires a thread-local command context for background rendering.
    /// @return a unique context containing a command pool and buffers.
    auto get_thread_context() -> thread_command_context;

    /// @brief returns a thread-local command context back to the pending queue.
    /// @param ctx the context to return.
    /// @param window the window this context was used for.
    /// @param frame_index the frame index this context was used for.
    auto return_thread_context(thread_command_context&& ctx,
                               ic_window*               window,
                               u32                      frame_index) -> void;

    /// @brief resets all pending contexts for a specific window and frame index.
    /// @param window the window whose contexts should be reset.
    /// @param frame_index the frame index whose contexts should be reset.
    auto reset_thread_contexts(ic_window* window, u32 frame_index) -> void;

    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief perform vulkan initialization.
    /// @return true if initialization succeeded, false if it failed.
    [[nodiscard]]
    auto initialize() -> bool;

    /// @brief perform vulkan shutdown and cleanup.
    auto shutdown() -> void;

    // --- vulkan initialization ---

    /// @brief create the vulkan instance with extensions and validation layers.
    /// @return true if instance creation succeeded, false if it failed.
    [[nodiscard]]
    auto create_instance() -> bool;

    /// @brief set up the debug messenger for validation layer output.
    /// @return true on success, false on failure. always true when validation
    /// is disabled.
    [[nodiscard]]
    auto setup_debug_messenger() -> bool;

    /// @brief select the best physical device (gpu).
    /// @return true if a suitable device was found, false otherwise.
    [[nodiscard]]
    auto pick_physical_device() -> bool;

    /// @brief create the logical device and retrieve queue handles.
    /// @return true on success, false on failure.
    [[nodiscard]]
    auto create_logical_device() -> bool;

    // --- surface helpers ---

    /// @brief create a platform-specific vulkan surface for a window.
    /// @param window the window to create a surface for.
    /// @return raii surface handle, or nullptr on failure.
    [[nodiscard]]
    auto create_surface_for_window(ic_window* window) -> vk::raii::SurfaceKHR;
};

}  // namespace vent
