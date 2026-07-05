# vent engine — in-depth architecture & correctness review

**date:** 2026-07-05
**scope:** full codebase (~19k lines) — every module, plugin, container, and cmake file was read.
**verification:** the project was built (`windows-debug`) and `minimal.exe` was run end-to-end under the
vulkan validation layer. findings marked **[observed]** were reproduced live; findings marked
**[latent]** are correct-by-luck today and will break when timing, hardware, or features change.

this document is written to teach, not just to list. every major finding explains the *underlying
concept*, because the bug class matters more than the bug.

---

## table of contents

1. [the architecture as it exists today](#1-the-architecture-as-it-exists-today)
2. [what is genuinely good](#2-what-is-genuinely-good)
3. [critical findings — threading & lifetime](#3-critical-findings--threading--lifetime)
4. [critical findings — gpu synchronization](#4-critical-findings--gpu-synchronization)
5. [serious findings — design & correctness](#5-serious-findings--design--correctness)
6. [agents.md conformance violations](#6-agentsmd-conformance-violations)
7. [performance review](#7-performance-review)
8. [code quality & uncommon-code notes](#8-code-quality--uncommon-code-notes)
9. [the big architectural tension](#9-the-big-architectural-tension)

---

## 1. the architecture as it exists today

reconstructed from the code and your artifacts (notably the multithreaded-rendering blueprint):

```
launcher.exe                        (templates/launcher.cpp — dumb loader)
  └─ libvent_engine.dll             (core + all static modules, whole-archive)
       ├─ system_registry           owns all systems, name → system_entry
       │    ├─ system_creator       static-init registration → creation → parallel init
       │    ├─ plugin_manager       dlopen/LoadLibrary wrapper
       │    └─ main_loop            client->is_running() driven frame loop
       ├─ bootstrap systems         log(-50) → event_bus(0) → job(100), sequential, main thread
       ├─ regular systems           platform, renderer, asset, world (+ client)
       │                            initialized IN PARALLEL on job workers,
       │                            dependency-ordered via "system.ready.*" events
       └─ plugins (runtime dlopen)
            ├─ libvent_vulkan_backend.dll   loaded BY the renderer during its own init
            └─ libclient_minimal.dll        loaded at startup, registers the client
```

**threads at runtime:** MAIN (frame loop), PLAT (glfw event pump + window ownership),
VLOG (async log writer), W:00..W:NN (job workers, one per hardware thread).

**render data flow (per window, per frame, all on MAIN unless noted):**

```
client on_update()
  └─ renderer()->begin_frame(w)  → backend begin_frame → fence wait, acquire, begin primary cmd + dynamic rendering
  └─ renderer()->end_frame(w)
       ├─ lazy-create default pipeline (loads app:// shader on first frame!)
       ├─ update_global_uniforms(view, proj)          → memcpy into per-frame-index UBO
       ├─ walk world()->get_renderable_entities()
       │    ├─ resolve mesh/texture through string-keyed caches
       │    └─ synchronous load + gpu upload on cache miss (blocking, waitIdle)
       ├─ build command_list → sort → chunk (256)
       ├─ job()->submit(record_command_chunk)  [W:xx] → secondary command buffers
       ├─ task.get() (main thread spin-helps)
       ├─ executeCommands(secondaries) into primary
       └─ backend end_frame → barrier, submit, present
```

the **design intent** i read out of this: a fully async engine where systems boot in parallel,
communicate through events, and the renderer records command buffers across all cores — with the
client seeing none of it. that intent is sound and matches AGENTS.md. the findings below are mostly
about places where the *implementation* of that intent outran the synchronization discipline it
requires.

---

## 2. what is genuinely good

worth saying explicitly, because these are the parts to keep and build on:

- **frontend/backend renderer split.** `i_render_backend` is a clean, api-agnostic contract. the
  backend contains zero scene logic. this is exactly the unreal-RHI shape and it's already paying
  off (the push-constant migration touched the backend only).
- **deferred surface destruction** (`marked_for_destruction`, swept in `begin_frame`). you
  correctly identified that destroying a swapchain from an event callback while the render thread
  is mid-frame is a race, and moved destruction to a known-safe point in the frame. this is the
  right *pattern* — the roadmap generalizes it to all gpu resources (deletion queues).
- **thread command-context pooling** in the vulkan backend. per-(window, frame-index) pending
  lists, reset only after the fence proves the gpu is done — the reasoning in those comments is
  correct and the design is legitimately advanced.
- **the mpmc queue is a textbook vyukov bounded queue** and the implementation is correct,
  including the sequence-number state machine and the release/acquire pairing.
- **the work-stealing deque is a near-correct chase-lev** (see §8 for the two soft spots). getting
  this even 95% right unassisted is rare.
- **platform thread with command marshalling.** `invoke_on_platform_thread` with inline execution
  when already on the platform thread is the correct shape for glfw's thread-affinity rules.
- **thread_registry naming + log integration.** `[W:08]`, `[PLAT]`, `[MAIN]` in every log line
  made this review dramatically easier. this is what good observability feels like — protect it.
- **the async log system** (mpsc queue + semaphore + worker, critical-flush handshake) is a solid
  design. note the irony in §7: debug builds bypass it entirely.
- **raii discipline in the vulkan plugin.** `vk::raii` everywhere, declaration order = destruction
  order documented in the header. the two leaks found are in *manual* vma objects, which proves
  the raii instinct right.

---

## 3. critical findings — threading & lifetime

these are ordered by how much they matter for the engine's future, not how soon they crash.

### C-1. the system registry's maps are mutated and read concurrently — **[latent, happens every boot]**

**where:** `system_registry.cpp` — `_systems`, `_interfaces` (the header even has
`// todo: does this ened a mutex?` at `system_registry.hpp:44`. answer: yes.)

**the concrete race that ships today:** regular systems initialize in parallel on job workers.
the renderer's init (on, say, W:08) calls `registry.load_plugin_library("vent_vulkan_backend")` →
`initialize_plugin_systems` → `create_from_pending` → `add_system` → **`_systems.emplace(...)` and
`_interfaces.emplace(...)`**. at that same moment, other workers initializing asset/world/platform
are calling `get_entry()`, `is_ready()`, and every `log()`/`event()`/`platform()` accessor walks
`_interfaces` via `get_interface_ptr()`. an `emplace` can rehash; a rehash invalidates every
iterator and bucket pointer a concurrent reader holds.

**why it hasn't crashed:** the maps are small (≤10 entries), rehashes are rare, and the window is
microseconds wide. this is the classic "works on my machine" data race — it is UB every single
boot, and it will manifest as an unreproducible startup crash the week you add your 15th system.

**the concept:** `std::unordered_map` gives you *no* thread safety, not even
readers-concurrent-with-one-writer. once you decided init runs on the job system, every registry
structure became shared state and needs either (a) a `std::shared_mutex`, (b) immutability during
the parallel phase (build the map fully, *then* fan out), or (c) a concurrent map. option (b) is
the cheapest and most deterministic: **create all systems before firing any init job, and never
add to the maps while jobs are in flight.** the nested `create_from_pending` from inside the
renderer's init is the one violator — see C-2 for how to fix both at once.

also affected: `system_entry.state` is written by `mark_system_ready` (worker) and read by
`is_ready` (other workers) as a plain enum — torn/stale reads are UB. make it
`std::atomic<system_state>`.

### C-2. dependency-event subscriptions capture stack frames by reference and are never unsubscribed — **[latent UB]**

**where:** `system_creator.cpp:449-484` (`initialize_regular`).

the subscription lambda is `[&ctx, name, dep, &fire_init]`. both `ctx` and `fire_init` live on
`initialize_regular`'s stack. the subscriptions are stored in `entry.event_subscriptions` but
**nothing unsubscribes them when the batch completes** — `shutdown_all` only cleans up systems
stuck in `awaiting_event`. so after `initialize_regular` returns, the event bus permanently holds
callbacks with dangling references to a dead stack frame.

**when it detonates:** the moment any `system.ready.<name>` event fires again for a name that was
ever a dependency — i.e., the first time you hot-reload a plugin or re-initialize a system. that's
a feature explicitly on your roadmap. the crash will point at the event bus and you'll lose a
weekend to it.

**second bug in the same code:** the callback does `std::erase(e->pending_dependencies, dep)` and
then checks `empty()`. two dependencies going ready simultaneously → two job-thread callbacks
mutate the same `std::vector` unsynchronized (UB), and both can observe `empty()` → **double
`fire_init`** → a system initialized twice concurrently.

**the concept:** *a callback registered with any long-lived dispatcher must only capture things
whose lifetime you can prove exceeds the subscription.* by-reference captures of locals are only
safe when the subscription provably dies before the frame does — which means unsubscription must
be part of the same control flow that subscribed (RAII subscription guards are the clean pattern:
a `scoped_subscription` type whose destructor unsubscribes).

**fix direction:** after the `first_pass_count` wait-loop completes, walk all entries and
unsubscribe every dependency subscription. give the per-entry dependency state a tiny mutex or an
atomic counter (`remaining_deps.fetch_sub(1) == 1` → fire) instead of vector-erase — an atomic
countdown is the canonical lock-free way to express "when N things are done, do X".

### C-3. the event bus provides weaker guarantees than its callers assume — **[design gap]**

**where:** `event_bus_system.cpp`.

what the bus actually guarantees today: callbacks run *sometime later, on any job worker,
in any order, possibly concurrently with each other, with anything, and after unsubscribe*.
specifically:

1. `publish()` fires one job per callback — two sequential publishes can execute in reverse
   order. dependency logic in C-2 implicitly assumes ordering.
2. `unsubscribe()` erases the entry but does **not** wait for in-flight callbacks. the renderer's
   shutdown unsubscribes `window.created` and then immediately tears down `_windows` and
   `_backend` — a callback snapshotted microseconds earlier can still be running against
   half-destroyed state. this is the classic *unsubscribe-vs-inflight* problem; every serious
   event system defines the answer (e.g., "unsubscribe blocks until callbacks drain", like
   `std::jthread::join`, or "callbacks hold a shared_ptr to their subject").
3. the `subscription::valid` flag is **dead code** — nothing ever sets it false
   (`unsubscribe` hard-erases instead), so `cleanup_invalid_subscriptions()` scans for a state
   that cannot exist. this is an inconsistent half-migration between two designs; pick one.
4. `_publish_count_after_cleanup++` is a non-atomic RMW on a `u32` from many threads (the `todo`
   suspects this). technically UB, practically a missed-cleanup counter.
5. **deadlock pattern to be aware of:** `platform_system::create_window` runs on PLAT and calls
   `publish_wait("window.created")`. the callbacks run on job workers. if any subscriber ever
   calls back into `invoke_on_platform_thread` (any `window::set_title/resize/...`), you get:
   PLAT waits for callback → callback waits for PLAT → **deadlock**. today the renderer's callback
   only touches vulkan and gets away with it. one innocent `w->set_title(...)` in a future
   subscriber freezes the engine. document the rule ("window.created subscribers must not marshal
   to the platform thread") or make `publish_wait` from PLAT illegal.

**the concept:** an event bus is not a synchronization primitive — it's a *scheduling* primitive.
every guarantee (ordering, delivery thread, unsubscribe semantics, reentrancy) must be an explicit,
documented decision, because subscribers will otherwise each assume a different contract.

### C-4. asset caches: TOCTOU races that hand out dangling pointers — **[latent]**

**where:** `asset_system.cpp` (`load_model`, `load_image`), mirrored by
`renderer.cpp` (`_model_cache` / `_texture_cache`).

`load_model` checks the cache under the lock, *releases the lock*, does the slow load, re-locks and
does `_model_cache[path] = std::move(asset)`. two threads racing the same path → both miss → both
load → the second assignment **destroys the first `model_asset` while the first caller holds a raw
pointer to it**. use-after-free.

same file, different flavor: `load_shader` never touches `_shader_mutex` at all — the mutex exists
and is simply unused. and the renderer's own caches are accessed with no lock in `end_frame` but
under `_model_mutex`/`_texture_mutex` in `shutdown()` — locking only one of two racing paths is the
same as not locking.

**the concept — check-then-act:** the check (cache miss) and the act (insert) must be atomic
*with respect to each other*, or two actors both pass the check. the standard patterns:
- double-checked insert: re-check under the lock before inserting; if present, drop your copy and
  return the existing one.
- or per-key in-flight markers ("loading" future in the map) so the second caller waits on the
  first load instead of duplicating it — this is what a real async asset manager needs anyway.

**and the deeper design smell:** these caches hand out **raw owning-ish pointers** with no
lifetime contract. `release_model(ptr)` frees while the renderer still holds `cached_m.asset`.
the roadmap's answer is handles + refcounts (see roadmap phase 2); the review's answer is: until
then, treat the caches as append-only and never call release during a frame.

### C-5. client-visible window pointers dangle after platform-initiated close — **[latent]**

**where:** `platform_system::process_close_requests` → `destroy_window` (PLAT thread) vs
`minimal.cpp`'s `_windows` vector (MAIN thread).

when the user clicks X, PLAT destroys the `window` object (`destroyed_window.reset()`), publishes
`window.destroyed` (renderer cleans *its* list), but the **client's own copy of the pointer is now
dangling** and the client keeps calling `renderer()->begin_frame(dangling)` every frame. today
that survives because `begin_frame` only *compares* the pointer against its surface list and finds
nothing. the first time anyone dereferences (`window->show()`, `get_title()` in a log line...),
it's a use-after-free that only reproduces when a user closes an auxiliary window at runtime.

**the concept:** raw pointers across ownership domains (engine owns, client borrows, a *different
thread* deletes) cannot express "this may die under you". the fixes, in ascending robustness:
(a) generation-checked handles (`window_handle { u32 index; u32 generation; }`) — the idiomatic
game-engine answer; (b) `std::shared_ptr/weak_ptr`; (c) event-driven invalidation the client must
cooperate with (fragile). the roadmap picks (a) for all public resources, windows included.

### C-6. `thread_registry::exit_thread()` — deliberate thread murder skips all destructors — **[uncommon code, remove]**

**where:** `thread_registry.hpp:129-137,162`.

`spawn_thread`'s wrapper ends with `exit_thread()` → `ExitThread(0)` on windows. consequences:
- the lambda's captured objects (`name`, `func` and everything `func` captured) are **never
  destroyed** — `ExitThread` does not unwind the stack. every spawned thread leaks its closure.
- the MSVC/mingw CRT is entitled to per-thread cleanup on normal return; `ExitThread` from inside
  C++ code bypasses `thread_local` destructor ordering guarantees.
- it buys you nothing: simply *returning* from the thread function does everything you want.

on linux `pthread_exit` forces unwinding (slightly less bad, still pointless). delete the call;
`std::thread` already handles clean exit. this is exactly the kind of "os-level power tool where
the language feature suffices" that's worth internalizing: **prefer structured exits (return)
over teleporting control flow (ExitThread/longjmp/exit), because C++ correctness is built on
unwinding.**

### C-7. blocking waits inside the job system can self-deadlock — **[latent]**

two related shapes:

1. **nested batch inside a job:** the renderer's `on_initialization` runs *on a worker* and calls
   `initialize_plugin_systems` → nested `initialize_regular` → `ctx.jobs->fire(...)` and then the
   *outer main thread* waits on `first_pass_count`. the nested batch's jobs must be picked up by
   *some* thread. with today's 12 workers that's fine; run with `--no_async`-adjacent configs, a
   2-core VM, or a future "low-power" worker count of 1, and a worker blocked waiting for a job
   that only it could execute is a deadlock. **rule to adopt: a job may never block on the
   completion of another job unless the waiter actively executes jobs from *all* queues while
   waiting** — which leads to:
2. **`wait_for_state` / `help_with_work_external` only drains the three global queues** —
   never local deques (its own or steals). a waited-on job sitting in a worker's local deque is
   invisible to the waiter; the waiter spins (`yield` loop, burning a core — see §7) until some
   worker gets to it. help-based waiting must be able to reach every queue the awaited job could
   be in, or use a real blocking primitive (`std::atomic::wait` on `state->completed` — c++20
   futex-backed, cheap, and you already have the atomic).

### C-8. `main_loop::set_runnables` / `cache_role_interfaces` race the running loop — **[acknowledged todo, elevating it]**

`cache_role_interfaces()` is called from `initialize_plugin_systems`, which is reachable at
*runtime* from any thread that loads a plugin — and it does `_main_loop.set_runnables(...)` which
assigns the vector the loop thread iterates with zero synchronization. your own comment
(`system_registry.cpp:608`) knows. the clean fix you already designed exists in `main_loop`
itself: `add_runnable`/`remove_runnable` + `sync_runnables()` at frame start is a correct
double-buffered pattern — `cache_role_interfaces` just bypasses it via `set_runnables`. route all
runtime mutation through the pending lists and keep `set_runnables` init-only.

---

## 4. critical findings — gpu synchronization

### G-1. one global UBO array, indexed by *per-swapchain* frame index, shared by all windows — **[latent gpu race]**

**where:** `vulkan_backend_system.cpp:990-1000`, `record_command_chunk` descriptor binding.

each swapchain has its own `_current_frame` counter, but there is exactly **one** set of
`_global_uniform_buffers[3]` / `_global_descriptor_sets[3]` for the whole backend. window A
submits work reading UBO[0]; window B — whose *own* frame-0 fence is long signaled — reaches its
frame 0 and memcpys new camera data into UBO[0] **while window A's submission is still executing
on the gpu**. the fence wait in B's `begin_frame` proves nothing about *A's* work.

today all windows share one camera so the race writes identical bytes and is invisible. the very
first feature that gives windows different cameras (editor viewports!) produces flickering,
inexplicably-wrong matrices under load. the same object also breaks:

- **descriptor updates while bound** (`create_texture` step 6 writes binding 1 of *all three*
  sets — including sets referenced by in-flight command buffers; VUID violation that passed only
  because your single texture uploads on the very first frame before anything is in flight).
- **only one texture can exist.** binding 1 is global; every texture upload overwrites it, and
  `render_packet.texture` is ignored by `record_command_chunk`. multi-textured scenes silently
  sample the last-loaded texture.

**the concept — frame resources:** anything the cpu writes and the gpu reads needs one copy per
*in-flight frame*, and the index must be scoped to whatever the fence actually protects. per-window
rendering ⇒ per-window (or per-window-per-frame) uniform buffers, or one global ring where the
slot is retired by the *specific* submission that used it. the roadmap's `frame_context` object
formalizes this.

### G-2. hardcoded "3" vs. driver-chosen image counts — out-of-bounds on real hardware — **[latent crash]**

three places assume ≤3 frames in flight:
- `create_global_uniforms`: `const u32 max_frames = 3;` with the comment
  `// MAX_FRAMES_IN_FLIGHT assumption`
- `_pending_contexts[3]` (`vulkan_backend_system.hpp:241`)
- `_global_descriptor_sets[current_frame]` unchecked in `record_command_chunk`

meanwhile `vulkan_swapchain::create_swapchain` does
`_max_frames_in_flight = _images.size()` — **the driver picks the image count**
(`minImageCount + 1`, clamped). plenty of drivers return 4+ (mailbox commonly ⇒ 4). frame index 3
then indexes a 3-element array and a 3-element C array: memory corruption on someone else's gpu.
`update_global_uniforms` bounds-checks and silently skips instead — so the camera freezes. both
failure modes are the bad kind: hardware-dependent.

also: that same line makes `set_frames_in_flight()` a lie — whatever the client requests is
overwritten by the image count at the next recreation, and `recreate()` doesn't rebuild sync
objects/command buffers to the new count anyway. **decide the model:** frames-in-flight is a
*cpu-side* constant (2 is the sweet spot), independent from swapchain image count. define
`MAX_FRAMES_IN_FLIGHT = 2` in one header, size *everything* off it, and keep per-image objects
(present semaphores) sized by `_images.size()` — you already do the latter correctly with
`_render_finished_semaphores`.

### G-3. mesh destruction without gpu sync — **[observed]** the validation error from the live run

`renderer_system::shutdown()` destroys cached meshes via `destroy_mesh` *before anything waits for
the last frame*. `destroy_texture` calls `_device.waitIdle()`; `destroy_mesh` doesn't. observed:

```
vkDestroyBuffer(): can't be called on VkBuffer 0x4e... currently in use by VkCommandBuffer
(VUID-vkDestroyBuffer-buffer-00922)
```

the per-resource `waitIdle` in `destroy_texture` is also the wrong shape (it's a full-device stall
per texture). the right pattern for both, until the deletion-queue phase of the roadmap: add
`i_render_backend::wait_idle()` and call it **once** at the top of the renderer's shutdown; strip
the per-resource waits.

### G-4. sync-blocking uploads inside the frame — **[design, observed as a frame hitch]**

`create_mesh` and `create_texture` each allocate a one-shot command buffer, submit, and
`_graphics_queue.waitIdle()` — inside `end_frame`, on first touch of an asset. the viking room's
first frame pays disk read + obj parse (~150k index dedup through `std::unordered_map<vertex,u32>`)
+ two full gpu stalls. one asset ⇒ one hitch; a real scene ⇒ seconds of frozen frames. this
is the single biggest obstacle between the current code and the "plug-n-play, never think about
frames" goal, and it's phase 2 of the roadmap (async asset pipeline + staging ring + placeholder
resources).

also: both upload paths lock `_mesh_mutex` around submit+waitIdle — a texture upload therefore
blocks every concurrent mesh lookup in `record_command_chunk` for the full gpu drain (lock held
across a device stall = worst-case lock hold time).

### G-5. `record_command_chunk` details

- `_pipelines.at(packet.pipeline)` (three times per packet, `vulkan_backend_system.cpp:1146-1154`)
  throws on a stale handle — from a job thread; `submit_internal` catches it into `task_state`, so
  `task.get<void*>()` **rethrows on the main thread mid-frame** where nothing catches it →
  `std::terminate`. look up once into a local, skip the packet on miss, log once.
- `_pipelines` is read here with **no lock** while `create_graphics_pipeline` /
  `destroy_graphics_pipeline` mutate it from the frontend thread. today creation happens before
  the first recording job and destruction after the last; the invariant is real but undocumented
  and one lazy-pipeline-creation away from breaking. either lock it or write the invariant down
  in the header ("pipelines are immutable between begin_frame and end_frame").
- the `_active_pipeline` bind at :1108 is dead — every packet re-binds. delete `bind_pipeline`
  and `_active_pipeline` entirely (AGENTS: remove unused code) or make packets *not* re-bind when
  the pipeline doesn't change (that's the point of sorting — see §7).
- `create_graphics_pipeline` reads `_surfaces[0]` without the shared mutex and bakes window 0's
  format into a pipeline used for **all** windows. two monitors with different surface formats
  (srgb vs unorm) render wrong on window 2. dynamic-rendering pipelines are cheap to key by
  format: cache per `(shader, color_format, depth_format)`.

### G-6. swapchain odds and ends

- `vulkan_swapchain` constructor reports failure via `log()->error` and leaves a zombie object —
  callers can't tell. constructors that can fail should throw (you already catch
  `std::exception` at the call site!) or move to a `create()` factory returning
  `std::optional`/`unique_ptr`.
- `recreate()` calls `_device.waitIdle()` *and* `wait_for_fences()` — the waitIdle makes the
  fence wait redundant; keep one (prefer the fences: per-swapchain, not device-global).
- backend `shutdown()` destroys `_allocator` while `_textures`/`_meshes` might still hold vma
  allocations if the frontend missed any — the raii members destruct after `shutdown()` returns,
  at which point the allocator is gone (image views/samplers are fine; the `VkImage`+allocation
  leak). drain `_textures`/`_meshes` in `shutdown()` before `vmaDestroyAllocator`.

---

## 5. serious findings — design & correctness

### S-1. the client is doing the engine's job — **[the #1 gap vs. your stated goal]**

`minimal.cpp` calls `begin_frame`/`end_frame` per window, owns the camera matrices, computes its
own aspect ratio (hardcoded `1280/720` even for the 1920×1080 window — the delayed window renders
stretched right now), and must remember the begin/end pairing discipline. AGENTS.md says *"the
client developer NEVER has to even look into renderer modules"*. the current sdk makes the client
the render-loop author.

target shape (roadmap phase 1): the renderer becomes an `ir_runnable`; the main loop calls it
after the client's `on_update`; it iterates windows itself; the camera is a *component on an
entity* (`camera_component { fov, near, far }` + per-window "active camera" assignment); aspect
ratio comes from the actual framebuffer. `minimal.cpp` shrinks to: create window(s), create
entities, done. that's the unreal feel you're aiming for, and everything needed already exists —
it's a responsibility move, not new tech.

### S-2. the renderer frontend hardcodes a client asset path

`renderer.cpp:213` loads `"app://assets/shaders/shader.slang.spv"` — an engine module reaching
into the app's mount. inverts the sdk principle and breaks any app that doesn't ship that exact
file. the engine has a designated home for this (`vent_engine/assets/` + the cmake shader
pipeline): ship a `vent://` engine mount with a default/error shader (the magenta-checkerboard
tradition exists precisely for this), mounted by the asset system itself relative to the
**executable's directory**, not the cwd — `mount("app://", ".")` breaks the moment the exe is
launched from anywhere else (this review had to `cd` into the app dir to run it).

### S-3. system-init dependency machinery: clever, but unfinished and over-general

honest assessment of `system_creator`: the event-driven dependency graph is the most complex code
in the engine, and it currently delivers *less* than a 30-line topological sort would:

- `await_event` staged-init is half-implemented (`setup_init_events` is an empty stub, the
  `awaiting` count is never populated, `result.ready` is "simplified for now").
- correctness holes C-1/C-2/C-3 all live here.
- boot is nondeterministic: init order varies run to run, which means boot bugs don't reproduce.
- the actual dependency graph today is tiny and diamond-free.

**recommendation (kiss + async-only reconciled):** compute a topological sort once (cheap,
deterministic, cycle-detecting — you currently have *no* cycle detection: two systems depending
on each other = infinite hang with no diagnostic), then execute it in **parallel waves**
(`parallel_for` over each wave). you keep multicore init, gain determinism and cycle errors, and
delete the event subscriptions entirely. keep staged-init as a *documented future feature* rather
than half-alive code (AGENTS: no dead code).

### S-4. failed asset loads retry forever, every frame

`renderer.cpp:240` — `_model_cache[path]` default-constructs on miss; a failed load leaves
`mesh == INVALID` so every frame re-reads disk and re-logs the error, per entity, per window.
add a `failed` flag to the cache entry (and later, the async pipeline's error-placeholder mesh
makes failures *visible* instead of log-spam).

### S-5. `minimal_client` uses `world()` without declaring the dependency

`dependencies()` lists platform/renderer/asset; `on_initialize` calls `world()->create_entity()`.
it works because world (dep: log only) practically always wins the race — but the parallel init
makes no such promise; this is exactly the nondeterminism S-3 warns about. one line fixes it.
(the same applies to any future system: *the deps list is the contract, the registry assert is
the enforcement* — consider asserting in debug that a system only `get<>`s interfaces it declared.)

### S-6. renderer window bookkeeping is redundant with the platform's

the renderer keeps its own `_windows` vector (mutex, subscriptions, duplicate-checks against
`plat->get_windows()`) but *never renders from it* — actual rendering is driven by the client
passing windows in, and surfaces live in the backend's `_surfaces`. three lists tracking the same
things is two too many. once S-1 lands (renderer iterates windows itself), keep exactly one owner:
platform owns windows, backend owns surfaces, renderer owns neither.

### S-7. world_system: fine for now, be intentional about its future

it's an honest map-based ecs-lite and that's *correct* for this stage — don't rebuild it as an
archetype ecs yet. but two notes: (a) zero synchronization while `get_renderable_entities()` is
consumed during `end_frame` — the moment gameplay mutates the world from a job while rendering
reads it, everything here races. the roadmap's extract-snapshot pattern (§roadmap phase 1) fixes
this without locks. (b) `entity` reuse: ids are never recycled and `_active_entities` is a vector
you `std::erase` from (o(n)); fine at this scale, note it for later.

### S-8. `VENT_API` is empty on windows

`vent_sdk.hpp:37-39`: on windows `VENT_API` expands to nothing; the engine dll works only because
mingw exports ~everything by default and modules are linked whole-archive. that silently negates
the "public footprint as small as possible" rule on your primary platform *and* would break
instantly under msvc. define it properly (`__declspec(dllexport/dllimport)` keyed by a
`VENT_ENGINE_EXPORT` define, like you already do for `VENT_PLUGIN_API`) and turn on
`-fvisibility=hidden`-equivalent discipline on windows via `--exclude-all-symbols` +explicit
exports, when you're ready to audit the surface.

---

## 6. agents.md conformance violations

the codebase follows its own rules impressively well overall (naming, trailing returns, headers,
lowercase comments are ~95% consistent). the violations that exist:

| # | rule | violation |
|---|------|-----------|
| A-1 | *"do NEVER add workarounds/hacks"* | `fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", "a")` **in the present path of every frame** (`vulkan_swapchain.cpp:285,312,490,504,526`). hardcoded absolute path, file open+close per frame per window, committed to the repo. this is the single most urgent deletion in the codebase — it's also a *performance* bug (see §7). |
| A-2 | sdk-minimal | `vent_module.cmake:92` adds `third_party/` as a **PUBLIC** include to *every* module — stb/tinyobjloader leak into every module and the sdk surface. only the asset module needs them; make it a private include there. |
| A-3 | sdk-minimal | `vent_sdk.hpp` carries renderer handles with your own `// todo: can we remove them from here?` — yes: move to `_vent/renderer/handles.hpp`. |
| A-4 | comment style (lowercase, period, doxygen) | `vertex.hpp` hash block (uppercase comments, no doxygen, crams the whole spec into unformatted lines); `asset_system.cpp:253` ("Vulkan uses 0 at top..."); `vulkan_backend_system.cpp:649,653` ("Exception during..."); `// system registratiob` typo at `vulkan_backend_system.cpp:1609`. |
| A-5 | struct layout docs + static_assert | `render_packet` documents offsets but has no `static_assert(sizeof(render_packet) == 96)` (job_t does this right — copy that pattern). |
| A-6 | *"always remove unused code"* | event bus `valid` flag machinery (§C-3.3); `bind_pipeline`/`_active_pipeline` (§G-5); `renderer.hpp`'s `i_device* _device` (never assigned, never used); `setup_dependency_events`/`setup_init_events` empty stubs; `ic_renderer.hpp` forward-declares `uniform_buffer_object` and includes `<memory>` unused; `mesh_component`-less `_active_entities` in world is write-only. |
| A-7 | *"macos is not supported → don't half-support"* (kiss) | `create_surface_for_window` cocoa branch contains a comment that should not survive a code review, and dead `#ifdef VENT_MACOS`. either support it or `#error`. |
| A-8 | error handling (log then return bool) | `vulkan_swapchain` constructor (can't return bool — see G-6); `world_system::destroy_entity` silently accepts unknown entities (fine, but the convention says log at trace). |
| A-9 | file organization / includes order | `work_stealing_deque.hpp` has **two** `#pragma once` (lines 1 and 14) and conditionally includes `<thread>` before the unconditional block; `ic_renderer.hpp` include order not alphabetized; `renderer.hpp` — `command_list _command_list;` member sits in a *public* macro-section header area between private blocks (three separate `private:` sections — the layout rule says members before methods, one order). |
| A-10 | cull-mode workaround | `cullMode = eNone` (changed from eBack in this diff) is a workaround for winding/handedness rather than a considered choice — with your RH z-up basis and the y-flip in `perspective`, the effective winding flips; the proper fix is picking the front-face convention once (`eClockwise` is what most y-flipping vulkan renderers land on) and documenting it in `mat4.hpp`. eNone costs you: every backface shaded, and z-fighting bugs stay hidden. |

---

## 7. performance review

ordered by measured/likely impact:

1. **the debug-file hack (A-1):** two `fopen/fprintf/fclose` per window per frame on the submit
   path. on windows, opening a file is a kernel round-trip plus av-scanner interaction —
   this alone can dominate your frame time. delete it and keep `vulkan_debug.txt` out of git.
2. **synchronous first-touch asset upload (G-4):** full-stall hitches; phase 2 roadmap.
3. **main-thread spin-wait on recording tasks:** `task.get()` → `wait_for_state` →
   `help_with_work_external` → **`std::this_thread::yield()` in a hot loop** when global queues
   are empty. with 1 mesh the recording job finishes in microseconds, but the pattern burns a
   core whenever workers are slow. replace the spin with `state->completed.wait(false)` /
   `notify_all` (c++20 atomic wait — you already have the atomic; this is a ~5 line change that
   also fixes the same spin in `drain()` and `parallel_for`).
4. **per-window scene extraction:** `end_frame` rebuilds+re-sorts the command list and re-walks
   the world once **per window** (3 windows = 3×). extraction belongs at frame level, once;
   per-window work should be "bind camera, replay packets". this halves-to-thirds your main
   thread cost immediately and is a prerequisite for per-window cameras anyway.
5. **string-keyed hot paths:** every entity, every frame, every window: two `unordered_map`
   lookups keyed by `std::string` (model path + texture path) including hashing the full path
   string; plus `world()->get_mesh(e)`/`get_transform(e)` = two more hash lookups through two
   virtual calls. handles fix this class entirely (mesh_component stores `mesh_handle` after
   first resolve).
6. **accessor cost:** every `vent::log()/world()/asset()` call walks `_interfaces` *linearly*
   comparing `type_index` **and** `strcmp`-ing mangled names (`system_registry.cpp:296-305` —
   the string compare exists for dll-boundary type_info mismatches, which is a real mingw
   concern, but it makes every accessor o(n·strcmp)). cache the resolved pointer per call site
   (static local in each accessor function) once systems are immutable-after-init — that also
   removes the C-1 read side.
7. **job system allocation churn:** every `fire`/`submit` = `new job_t` (128B) + for submits
   `new task_state` + `malloc` result. thousands/sec at scale. the classic fix is a pooled
   allocator (free-list of job_t) — your `// todo: memory system` comments already know.
8. **worker oversubscription:** `hardware_concurrency()` workers **plus** MAIN, PLAT, VLOG.
   12 workers + 3 busy threads on a 12-thread cpu = context-switch tax exactly when loaded.
   default to `hw - 2` (min 1); make it the first entry in the future config system.
9. **event-bus log spam:** `dispatch` trace-logs every event twice even with zero subscribers —
   at least gate behind a compile-time verbose flag; string-formatting costs are real even when
   the sink drops them (fallback log formats eagerly).
10. **log system in debug builds is bypassed entirely** (`accessors.cpp:49`: debug → always
    fallback printf log). that means: (a) your beautiful async pipeline runs untested in daily
    dev, (b) every debug log is a synchronous `printf` with global console lock —
    the 406-line boot log costs visible milliseconds. consider debug = async system + sync
    *flush policy* instead of a different code path (test what you ship).
11. **uncapped main loop:** no pacing when nothing is presented (all windows minimized ⇒ hard
    spin), no `delta_time` clamp (first-frame and debugger-pause deltas explode physics later).
    add: max-delta clamp now; frame pacing when no swapchain presented this frame.

not-problems worth noting: mailbox present mode selection, the sort in `command_list` (though the
key is currently the entity id — the blueprint's real key packing is roadmap phase 3), vma usage,
dynamic viewport/scissor to avoid pipeline recreation on resize. these are all correct calls.

---

## 8. code quality & uncommon-code notes

- **`work_stealing_deque::pop()` returns an *engaged* optional containing `T{}`** (i.e. a null
  `job_t*`) when it loses the last-item race to a thief (`:166`). `get_job` then returns that
  nullptr immediately, skipping the global-queue checks for that iteration, and the worker sleeps
  1ms with work possibly available. return `std::nullopt` on the lost race. (also: the retired-
  arrays list caps at 8 — after 8 grows a stale thief pointer is a UAF; with 1024 initial capacity
  that's ~256k jobs in one deque, effectively unreachable, but a one-line comment should say so.
  the *canonical* solution is epoch/hazard reclamation — good future learning project.)
- **`mpmc_queue`**: correct. one nit: the ctor writes `_data = T{}` per cell — for
  non-trivial `T` that's fine, for `job_t*` it's redundant; and cells sharing cache lines is a
  known false-sharing cost your own todo mentions.
- **`fallback_log` in debug + `g_no_async`**: good idea (deterministic debugging), but
  `--no_async` only gates `job()` in `#if defined(VENT_DEBUG)` — in release the flag parses and
  silently does nothing. either support it everywhere or reject it in release.
- **`job_system::initialize` worker-count math**: the double-negative
  `(total > abs(0)) ? total + 0 : 1` collapses to `total`; the reserve-N-cores design the comment
  describes is unimplemented. simplify until the config system exists.
- **`system_registry::get_interface_ptr`** compares `type_idx.name()` *strings* before
  `type_index` equality — that ordering means the slow path always runs. flip it: compare
  `type_index` first, fall back to string only on miss (dll case).
- **`initialize_all` logging loop** does `_system_to_plugin[name]` (`system_registry.cpp:164`) —
  `operator[]` *inserts* empty entries for every non-plugin system and bypasses
  `_plugin_tracking_mutex`. use `.find()`.
- **`vertex`** — `f32 u, v;` instead of a `vec2 uv` (the attribute description depends on `v`
  following `u`); `operator==` compares floats memberwise (fine for dedup — but say so in a
  comment); the hash is weak (xor of shifted hashes collides on permutations — fine for a loader,
  again: comment the tradeoff). also the `std::hash` specialization needs the AGENTS-style header
  comment explaining *why* it exists (tinyobj dedup).
- **`window::get_native_handle`** calls `glfwGetX11Window`/`glfwGetWin32Window` from whatever
  thread the backend runs on. glfw documents native-access functions as callable from any thread,
  *but* `glfwGetPlatform()` inside it is main-thread-only-ish gray area; you already learned this
  lesson once (the `get_platform_type` comment). low risk; worth one comment.
- **`perspective`/`look_at`**: correct for RH z-up → vulkan clip, and the comments explaining the
  axis mapping are exactly what AGENTS wants. add the one missing piece: a comment stating the
  *winding* consequence (ties to A-10), and a `static_assert(sizeof(mat4) == 64)`.
- **duplicate `#pragma once`** in `work_stealing_deque.hpp`.
- the **client aspect-ratio bug**: `minimal.cpp:102` hardcodes `1280/720`; the 1920×1080 delayed
  window is stretched. becomes moot after S-1, worth knowing it's visible today.
- **`minimal.cpp` frame-60 window** is created without the null check its sibling loop has, and
  `on_shutdown` divides by `_elapsed` (0.0 if exit happens frame 1 → inf; cosmetic).

---

## 9. the big architectural tension

one meta-observation to carry into the roadmap:

**AGENTS.md principle 1 says "async-only: anything that can be async must be async". the majority
of the critical findings in this review exist because code was made concurrent before its data was
made concurrency-safe.** parallel system init (C-1/C-2/C-3), event callbacks on job threads (C-3),
parallel asset loads (C-4), multi-window gpu submission (G-1) — each is an *async mechanism*
bolted onto *synchronous-era data structures* (plain maps, raw pointers, stack captures, single
global buffers).

the mature version of principle 1 — the one unreal/naughty dog/id operate by — is:

> *parallelism is applied to data that was **designed** for it: immutable snapshots, ownership
> handoffs at explicit sync points, handles instead of pointers, and per-frame arenas. code that
> hasn't had that design pass runs single-threaded without shame.*

concretely for vent: it is perfectly fine — and more "professional", not less — for the world,
the registry-after-init, and the frame loop to be single-threaded *by contract*, while the truly
parallel parts (command recording, asset io, decompression) operate on data handed to them with
clear ownership. the roadmap is structured around exactly this: first shrink the shared-mutable
surface, then scale the parallelism back up on safe foundations.

that's not a retreat from your principle. it's how the principle survives contact with a 100k-line
codebase.

---

*companion documents:*
- `20260705_1800_claude_roadmap.md` — prioritized fix/refactor/build plan derived from this review.
- `20260705_1800_claude_threading_model.md` — reference sheet: current thread/data ownership map and the target rules.
