# Vent Engine: Multithreaded Rendering Architecture Blueprint

This document serves as the permanent blueprint for Vent's rendering architecture. Even if 10 years pass, this document accurately details the transformation of Vent from a single-threaded immediate-mode renderer to a fully parallelized, batch-sorted, Secondary-Command-Buffer powered modern rendering engine.

## The Goal
To completely decouple the game logic threads from the Vulkan Backend API, ensuring that:
1. The CPU can generate rendering commands lock-free across all threads.
2. The GPU receives perfectly sorted commands to minimize state thrashing (pipeline switches, descriptor binds).
3. The Vulkan Backend translates CPU commands into GPU commands in parallel using `vent_job` and Vulkan Secondary Command Buffers.

---

## Phase 1: Thread-Local Batching (Decoupling the Frontend)

In Phase 1, we replace direct Vulkan `vkCmdDraw` calls with intermediate `render_packet` structs stored in a `command_list`. 

### 1. The Sort Key & Render Packet (`render_command.hpp`)
We will create `c:/dev/vent/source/vent_engine/_vent/renderer/render_command.hpp`.

**Sort Key (64-bit):**
A 64-bit integer designed to perfectly sort draw calls to minimize GPU state changes.
```cpp
// 64-bit Sort Key Layout:
// [ 8 bits: Depth/Layer ] 
// [ 16 bits: Pipeline Handle ]
// [ 16 bits: Material/Texture Handle ]
// [ 24 bits: Distance to Camera (for transparent sorting) ]
using sort_key = vent::u64;
```

**Render Packet:**
A lightweight, 32-byte struct containing everything the backend needs to execute a draw.
```cpp
struct render_packet {
    sort_key    key;
    mesh_handle mesh;
    // ... eventually material/pipeline handles and a transform matrix index
};
```

### 2. The Command List (`command_list.hpp`)
A wrapper around `std::vector<render_packet>` that provides high-level rendering API calls (like `draw_mesh`).
```cpp
class command_list {
public:
    void draw_mesh(mesh_handle mesh, sort_key key);
    void sort(); // uses std::sort on the internal vector based on sort_key
    std::span<const render_packet> get_packets() const;
private:
    std::vector<render_packet> _packets;
};
```

### 3. Modifying `ic_renderer` and `i_render_backend`
We will rip out the immediate-mode `draw()` functions.

**In `ic_renderer.hpp`:**
```diff
- virtual auto draw_mesh(mesh_handle mesh) -> void = 0;
+ // Clients now ask for a thread-local command list to record into.
+ virtual auto get_command_list() -> command_list& = 0;
+ // Submitted at the end of the frame by the main thread.
+ virtual auto submit_command_lists(std::span<command_list*> lists) -> void = 0;
```

**In `i_render_backend.hpp`:**
```diff
- virtual auto draw_mesh(mesh_handle mesh) -> void = 0;
+ // Backend receives a perfectly sorted array of all packets for the frame.
+ virtual auto execute_packets(std::span<const render_packet> packets) -> void = 0;
```

---

## Phase 2: Vulkan Secondary Command Buffers (Parallelizing the Backend)

Once Phase 1 is done, the backend receives a single, sorted `std::span<const render_packet>` representing the entire frame. In Phase 2, we translate these packets into Vulkan Command Buffers **in parallel**.

### 1. Thread-Local Command Pools
In `vulkan_backend_system`, we cannot use a single `vk::CommandPool` because they are not thread-safe. 
Instead, we will allocate a `vk::CommandPool` **for each worker thread** in the `vent_job` system.

### 2. The Translation Job
When `execute_packets()` is called, we will chunk the `std::span<const render_packet>` and dispatch jobs to `vent_job`.

```cpp
// Pseudo-code inside vulkan_backend_system::execute_packets():

// Split 10,000 packets into 4 chunks of 2,500.
auto chunks = split_span(packets, 4); 

// Create an array to hold the resulting secondary command buffers.
std::vector<vk::CommandBuffer> secondary_buffers(4);

for (int i = 0; i < 4; ++i) {
    vent::job_system()->dispatch([this, chunk = chunks[i], &secondary_buffers, i]() {
        // 1. Get the thread-local command pool.
        auto pool = get_thread_local_pool();
        
        // 2. Allocate a vk::CommandBuffer with eSecondary level.
        vk::CommandBuffer cmd = allocate_secondary(pool);
        
        // 3. Begin recording.
        cmd.begin(vk::CommandBufferBeginInfo{ /* flags for secondary */ });
        
        // 4. Iterate over the chunk and translate to Vulkan!
        for (const auto& packet : chunk) {
            bind_pipeline(packet, cmd);
            bind_mesh(packet.mesh, cmd);
            cmd.draw(...);
        }
        
        cmd.end();
        secondary_buffers[i] = cmd;
    });
}

// Wait for all translation jobs to finish.
vent::job_system()->wait_for_all();
```

### 3. The Final Execution
Once all worker threads finish translating their chunks into `secondary_buffers`, the Main Render Thread does the final execution:

```cpp
// On the main thread, using the primary command buffer:
primary_cmd.executeCommands(secondary_buffers);

// Submit to the GPU once per frame!
graphics_queue.submit(primary_cmd);
```

---

> [!CAUTION]
> ### Review Request
> This blueprint radically changes the renderer from an immediate-mode (`draw()` directly calling Vulkan) to a retained/deferred-mode (`draw()` pushes to a `vector`, executed later in parallel). 
>
> If this architecture blueprint looks perfectly aligned with the long-term vision of Vent Engine, please approve and we will begin Phase 1: Creating `render_command.hpp` and modifying `ic_renderer`!
