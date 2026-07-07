# the vent handbook

**date:** 2026-07-07 · **status:** current as of phase 1 (engine-owned frame loop)
**audience:** everyone. section 1–3 need no programming knowledge. sections 4+ go progressively
deeper. ai agents: this file replaces scanning the codebase for orientation — read the table of
contents, jump to what you need, and use section 12 ("where do i change X") to find the exact
files for a task. this document is self-contained and does not require any other artifact.

> maintenance note: update this handbook when architecture-level facts change (new module, new
> frame phase, changed threading rules, changed mounts). style/code rules live in
> `.agents/AGENTS.md`, which always wins on conflicts.

---

## table of contents

1. [what is vent?](#1-what-is-vent) *(for everyone)*
2. [the goal — the north star](#2-the-goal) *(for everyone)*
3. [a frame of vent, told as a story](#3-a-frame-as-a-story) *(for everyone)*
4. [core principles & vocabulary](#4-principles-and-vocabulary)
5. [the big picture: processes, libraries, systems](#5-big-picture)
6. [directory map](#6-directory-map)
7. [boot: from double-click to first frame](#7-boot)
8. [the frame: phases, extraction, per-window rendering](#8-the-frame)
9. [module tour](#9-module-tour)
   - [core](#91-core) · [log](#92-log) · [event_bus](#93-event-bus) · [job](#94-job)
   - [platform](#95-platform) · [asset](#96-asset) · [world](#97-world)
   - [renderer](#98-renderer) · [vulkan_backend](#99-vulkan-backend)
10. [threading model](#10-threading)
11. [build system & sdk](#11-build)
12. [where do i change X? (task → file map)](#12-where-do-i-change-x)
13. [current limitations (honest edges)](#13-limitations)
14. [glossary](#14-glossary)

---

<a name="1-what-is-vent"></a>
## 1. what is vent?

vent (always lowercase) is a personal game engine written in modern C++ (C++23) using the
vulkan graphics api. a *game engine* is the software layer between "i have an idea for a game"
and "pixels move on screen": it opens windows, talks to the graphics card, loads models and
textures from disk, keeps time, and runs the game's own code every frame.

vent is a **learning project**: its purpose is to understand how engines and gpus really work,
by building one properly instead of reading about it. that shapes everything — the code is
heavily commented (comments explain *why*, like a lab notebook), correctness is prioritized
over shortcuts, and every phase of development is documented in `artifacts/`.

vent is not a product (yet). there are no external users, which is why the codebase has a
strict **no-legacy rule**: old code is deleted, never kept "for compatibility".

---

<a name="2-the-goal"></a>
## 2. the goal — the north star

the target experience, judged against this exact snippet: a game developer with a creative
background and **zero** graphics/systems knowledge writes

```cpp
auto on_initialize() -> bool override {
    vent::world()->spawn("app://models/viking_room.glb");
    return true;
}
```

…and gets a lit, textured, correctly-paced scene in a window they didn't have to create,
rendered by a graphics backend they cannot name. every design decision is measured against
that sentence.

the pillars behind it:

- **easy on the surface** — clients describe scenes; the engine renders them. the client never
  calls a render function (this became literally true in phase 1).
- **fast underneath** — multithreaded from boot (systems initialize in parallel on all cores)
  through the frame (command recording runs on a job system), with gpu work correctly
  synchronized rather than stalled.
- **modular** — features live in swappable static modules and hot-reloadable dynamic plugins.
  the vulkan backend is a plugin; a future dx12 backend would be too, with zero client change.
- **sdk-shaped** — the engine builds into a redistributable sdk. the public surface is kept as
  small as possible: if a client doesn't actively need it, it isn't exposed.

---

<a name="3-a-frame-as-a-story"></a>
## 3. a frame of vent, told as a story *(no programming knowledge needed)*

imagine the engine as a small film studio that produces ~60 finished pictures per second.

1. **the writers' room (simulate phase).** the game's own code runs first. it moves things:
   "the viking house rotates another 1.5 degrees." it doesn't draw anything — it only updates
   the *world*, a big ledger of everything that exists (entities) and their properties
   (components: where it is, what it looks like, whether it's a camera).
2. **the photocopier (extraction).** the renderer now takes a **snapshot** of the ledger — a
   private photocopy of exactly what it needs: which meshes, which textures, where, and where
   each window's camera stands. from this moment the original ledger could change freely; the
   picture being made won't tear, because it's made from the copy.
3. **the camera department (per window).** for every open window, the engine computes the view
   from *that window's* camera and the window's real size (resize a window: the picture adapts
   instantly).
4. **the assembly line (job workers).** the draw instructions are split into chunks and handed
   to worker threads — one per cpu core — which write gpu command lists in parallel.
5. **the delivery (submit & present).** the finished command lists are handed to the graphics
   card with a *fence* — a receipt the gpu signs when it's done. the engine never overwrites
   materials the gpu is still reading; it always waits for the right receipt first. the
   finished image appears in the window.
6. repeat, forever, until the game says stop.

two studio policies worth knowing: if a texture fails to load, the object is painted in a loud
**magenta-and-black checkerboard** instead of disappearing — a visible problem is a findable
problem. and the studio owns its own props (`engine_assets/` ships with every app), so a
missing game asset never means a black screen.

---

<a name="4-principles-and-vocabulary"></a>
## 4. core principles & vocabulary

the five principles from `.agents/AGENTS.md`, with their practical meaning:

| principle | in practice |
|---|---|
| **async-only** | anything that can run in parallel should — *on data designed for it* (snapshots, handoffs, handles). code without that design pass runs single-threaded without shame. on that note — *data should be designed for parallel usage from the start.* |
| **abstract-first** | client code stays simple; complexity hides behind interfaces. |
| **performance-aware** | measure before optimizing; comment every non-obvious trade-off. |
| **KISS** | prefer clear over clever. a 30-line boring solution beats a 10-line puzzle. |
| **sdk-based** | the engine ships as an sdk; internals must not leak through it. |

naming decoder (you will see these prefixes everywhere):

| prefix / suffix | meaning | example |
|---|---|---|
| `ic_*` | **c**lient-facing interface (public sdk) | `ic_world`, `ic_log` |
| `i_*` | engine-internal interface (modules see it, clients don't) | `i_render_backend` |
| `ir_*` | **r**ole interface (opt-in capability of a system) | `ir_runnable`, `ir_client` |
| `*_system` | concrete implementation of a system | `world_system`, `log_system` |
| `_*` | private member variable | `_backend`, `_initialized` |
| `UPPERCASE` | template type parameters (only exception to snake_case) | `template <typename TYPE>` |

everything else is `snake_case`, comments are lowercase ending with a period, functions use
trailing return types (`auto f() -> bool`).

---

<a name="5-big-picture"></a>
## 5. the big picture: processes, libraries, systems

at runtime, a vent application is four kinds of binary:

```
minimal.exe                       the LAUNCHER. tiny. loads the engine dll, calls its
  │                               entry point, passes the generated build info. that's all.
  └─ libvent_engine.dll           the ENGINE. all selected static modules linked in
       │                          (whole-archive). owns the system registry + main loop.
       ├─ libclient_minimal.dll   the CLIENT plugin. the game's code. loaded at startup,
       │                          registers a client_base subclass. hot-reloadable later.
       └─ libvent_vulkan_backend.dll
                                  a BACKEND plugin. loaded BY the (static) renderer module
                                  during its own initialization. implements i_render_backend.
```

inside the engine dll, everything is a **system**: an object inheriting `system_base` with a
unique name (`"vent.system.world"`), a lifecycle (`on_initialization`/`on_shutdown`), and
optional **roles**:

- `ir_dependencies` — "initialize me only after these named systems are ready."
- `ir_runnable` — "call my `on_update(dt)` every frame, in my declared frame phase."
- `ir_client` — "i am the application; i decide when we exit." (exactly one per engine.)
- `ir_bootstrap` — "i must exist before everything else" (log, event bus, job system. cannot co-exist with `ir_dependencies`).

the **system registry** (`modules/core/private/system/system_registry.hpp`) owns all systems,
resolves interfaces by type, drives initialization and shutdown, and hosts the main loop.

client code reaches systems through **global accessors** (`_vent/accessors.hpp`):
`vent::log()`, `vent::event()`, `vent::job()`, `vent::platform()`, `vent::asset()`,
`vent::world()`, `vent::renderer()` (now a marker only), and `vent::system()` for generic
`get<interface>()` lookups. 

clients include exactly one header: `<_vent/_vent.hpp>`. engine code will **not** include 
that header but rather only include specific required headers.

---

<a name="6-directory-map"></a>
## 6. directory map

```
vent/
  .agents/AGENTS.md            THE rulebook: conventions, contracts, workflow. read first.
  artifacts/                   curated ai-generated docs (reviews, plans, walkthroughs).
  cmake/                       build functions: vent_create_module / _plugin / _client,
                               vent_compile_shaders (slang → spirv), toolchain glue.
  build/                       all build output (gitignored). see section 11 for layout.
  source/
    vent_apps/
      minimal/                 example client + development testbed.
        assets/                app assets (viking room model/texture).
        src/minimal.cpp        the whole client. the north-star reference.
    vent_engine/
      _vent/                   ★ THE PUBLIC SDK. everything a client may include.
        _vent.hpp                single include for clients.
        vent_sdk.hpp             base types (u32/f64/usize…), macros, VENT_API.
        accessors.hpp            vent::log()/world()/… shortcuts.
        client_registration.hpp  client_base + VENT_REGISTER_CLIENT macro.
        core/                    role interfaces: ir_runnable (frame phases!), ir_client,
                                 ir_dependencies; ic_system_registry.
        math/                    vec3/vec4/mat4; look_at, look_at_transform, inverse_rigid,
                                 perspective (vulkan clip conventions documented in mat4.hpp).
        world/ic_world.hpp       entity api + transform/mesh/camera components.
        platform/                ic_platform, ic_window, window_desc.
        renderer/                ic_renderer (marker), render_command.hpp (packets/sort),
                                 handles, vertex, pipeline_desc, uniform_buffer, texture_desc.
        asset/                   ic_asset + asset structs (model/image/shader).
        log/ event_bus/ job/     ic_log, ic_event_bus, ic_job + task.
        system/                  system_base, init status, concepts.
      assets/shaders/          engine-owned shaders: default.slang, error.slang.
      modules/                 static libraries linked into the engine dll:
        core/  log/  event_bus/  job/  platform/  asset/  world/  renderer/
        (each: public/<name>/ = engine-internal headers other modules may use,
               private/       = module-private headers, src/ = implementation.)
      plugins/
        vulkan_backend/        the vulkan implementation of i_render_backend.
      templates/launcher.cpp   the default launcher source (copied per app).
      third_party/             stb_image, tinyobjloader (private to the asset module).
```

**the golden include rule:** clients include **only** `_vent/`. modules may include their own
private headers, other modules' `public/` headers, and `_vent/`. nothing outside
`plugins/vulkan_backend/` may ever even mention vulkan.

---

<a name="7-boot"></a>
## 7. boot: from double-click to first frame

1. **launcher** (`templates/launcher.cpp`): loads `libvent_engine.dll`, calls
   `vent_engine_main` with the cmake-generated build info (which startup plugins to load —
   at minimum `client_<app>`).
2. **bootstrap systems**, sequential, on the MAIN thread, in fixed order (hand-written by priority):
   **log** → **event_bus** → **job**. these three exist before anything else so every later
   system can log, publish, and parallelize from its first line.
3. **plugin load:** the client plugin dll is loaded; its static registration
   (`VENT_REGISTER_CLIENT`) adds the client to the pending system list.
4. **regular systems in parallel:** platform, renderer, asset, world, and the client are
   created, then initialized **concurrently on job workers**. ordering is enforced only by
   declared dependencies (`ir_dependencies`), signalled through `system.ready.<name>` events.
   the renderer's init loads and initializes the `vent_vulkan_backend` plugin itself.
   - ⚠ contract: a system may only use interfaces it *declared* as dependencies. undeclared
     use works by luck of init order and will eventually not.
5. **role caching:** the registry collects all `ir_runnable`s and the single `ir_client`,
   hands them to the main loop, applies the client's window-close policy.
6. **main loop** (`modules/core/src/main_loop.cpp`): runs until the client's `is_running()`
   is false, stop is requested, or the last window closes. per frame: **drain main-thread-
   pinned jobs** (`job()->run_pinned_jobs()` — runs `job_affinity::main` work and
   `event_delivery::main` callbacks at a safe point, before any phase) → integrate pending
   runnable changes → **stable-sort runnables by `run_phase()`** → compute `delta_time`
   (clamped to 100 ms so a debugger pause doesn't teleport physics) → call every runnable's
   `on_update(dt)` in phase order.
7. **shutdown:** reverse init order; the renderer waits the gpu idle **once**, destroys its
   caches and surfaces; plugins unload; bootstrap systems go down last.

the asset system's init also establishes the **default mounts** (section 9.6) — before any
client code runs, `vent://` and `app://` already resolve.

---

<a name="8-the-frame"></a>
## 8. the frame: phases, extraction, per-window rendering

the frame is a pipeline of **phases**, ordered by `ir_runnable::run_phase()`
(`_vent/core/ir_runnable.hpp`):

| phase | constant | who | world access |
|---|---|---|---|
| simulate | `run_phase_simulate` (0) | client, future gameplay systems | **mutation allowed** |
| render | `run_phase_render` (1000) | renderer_system | **read-only by contract** |

(gaps in the numbering are reserved for future phases: input, physics, extraction as its own
phase, post-frame.) within a phase, registration order is kept (stable sort) — the frame is
byte-for-byte deterministic across runs.

the renderer's tick (`modules/renderer/src/renderer.cpp::on_update`) has four named stages:

```
reconcile_surfaces()        converge gpu surfaces to the platform's window list: windows
                            without a surface get one; surfaces whose window vanished are
                            destroyed (deferred). runs on MAIN at frame start — deterministic,
                            no event callbacks on worker threads.
ensure_default_resources()  lazy one-time: default pipeline (vent://shaders/default.slang.spv,
                            fallback error.slang.spv = magenta), procedural 64² checkerboard
                            placeholder texture.
extract_frame()             THE SNAPSHOT. walk world renderables once: resolve model/texture
                            caches (sync load on first touch — phase 2 makes this async),
                            build + sort the command_list of render_packets (96-byte value
                            types: sort key, mesh/texture/pipeline handles, transform BY
                            VALUE). resolve each window's camera BY VALUE (pose + fov/near/
                            far). after this, rendering never reads live world state.
render_window(wc)           per window with a surface:
                              backend->begin_frame(w)      fence wait + acquire image
                              view = inverse_rigid(pose);  proj from fov + REAL framebuffer
                              backend->update_frame_uniforms(ubo)   → this window's ring slot
                              chunk packets (256/chunk) → job()->submit(record_command_chunk)
                              backend->execute_recorded_commands(secondaries)
                              backend->end_frame(w)        barrier, submit, present
```

**camera resolution** (in `world_system::get_active_camera`): explicit per-window assignment
(`set_active_camera(e, window)`) → default camera (`set_active_camera(e)`) → first entity with
a camera component (creation order — deterministic) → engine fallback camera (warns once).
every window always renders *something*.

**why extraction matters:** a snapshot converts a data race into a memcpy. because the render
stage reads only frozen copies, a future scheduler can overlap simulate(frame N+1) with
render(frame N) without a single lock. this seam is the engine's most important architectural
investment.

---

<a name="9-module-tour"></a>
## 9. module tour

<a name="91-core"></a>
### 9.1 core (`modules/core/`)

the chassis. contains:

- **system_registry** (`private/system/system_registry.hpp`): owns `name → system_entry`,
  interface lookup (`get<T>()` via type_index with a string-compare fallback for dll-boundary
  type_info mismatches — a real mingw concern), init/shutdown orchestration, role caching,
  the main loop instance.
- **system_creator** (`private/system/system_creator.hpp`): pending registrations (from
  static-init macros), creation vs. initialization split, the event-driven dependency wait
  logic for parallel init. ⚠ this is the most intricate code in the engine; the owner has
  decided it **stays event-based** — do not replace it with a topo-sort without asking.
- **main_loop**: phase-sorted runnable updates, delta clamp, pending add/remove double-buffer
  (any thread may add/remove runnables; the loop integrates at frame start).
- **plugin_manager**: LoadLibrary/dlopen wrapper with unload bookkeeping.
- **thread_registry** (`public/core/thread_registry.hpp`): names threads (`MAIN`, `PLAT`,
  `W:07`…) — these names appear in every log line. ⚠ `exit_thread()` is a deliberate,
  documented workaround for a toolchain teardown issue. keep it.
- **executable_path** (`public/core/utils/executable_path.hpp`): `get_executable_directory()`
  — the anchor for all deployment-relative paths.
- **containers** (`public/core/containers/`): `mpmc_queue` (vyukov bounded), `mpsc_queue`,
  `circular_array` (single-threaded backing store); plus `work_stealing_deque` (chase-lev) in
  `modules/job/private/`. hand-rolled lock-free structures used by job/log systems, covered by
  storm tests in `source/tests/` (target `vent_tests`, run via ctest). ⚠ treat any modification
  as high-risk; extend the storm test with it. the deque publishes its backing array
  **atomically** (release store on grow, acquire load in `steal`) and **retains outgrown arrays
  for its lifetime** (geometric growth ⇒ bounded to ~2× the final size), so a thief can never
  observe a stale or half-swapped array pointer.
- **fallbacks** (`private/fallback/`): synchronous log/job stand-ins used before bootstrap
  completes (and, for the log, throughout debug builds).

<a name="92-log"></a>
### 9.2 log (`modules/log/`)

async logging: producers push formatted entries into an mpsc queue; a dedicated **VLOG**
thread drains to console + rotating file (`logs/`). severity api: `trace/debug/info/warn/
error`, each with a category string: `log()->info("client", "hello {}", 42)`.
⚠ in **debug builds** the accessors return the synchronous fallback logger instead (printf-
style) — the async path effectively runs untested in daily development; know this when
debugging "logging behaves differently in release".

<a name="93-event-bus"></a>
### 9.3 event_bus (`modules/event_bus/`)

string-keyed publish/subscribe. **the documented contract** (in `ic_event_bus.hpp`) is
load-bearing — assume nothing beyond it:

- each subscription picks a **delivery policy** (`event_delivery`, chosen at `subscribe`):
  - `parallel` (default) — on a job worker, **no defined order**, possibly **concurrently**.
    the callback must be thread-safe. this is the engine's historical behavior.
  - `main` — deferred onto the main thread and run at the **frame-start drain**
    (`job_affinity::main` → `run_pinned_jobs`). serialized, never concurrent; the safe
    choice for callbacks that touch main-thread-owned state (the world, gpu surfaces).
  - `immediate` — synchronously on the publisher's thread, before `publish` returns.
- `publish` is fire-and-forget; `publish_wait` blocks until `parallel`/`immediate` callbacks
  complete but **does not block on `main` subscribers** (awaiting the frame drain from another
  thread would deadlock) — a `main` subscriber always runs at the next frame boundary.
- `unsubscribe` does **not** wait for in-flight callbacks.
- deadlock rule: a `parallel` subscriber of `window.created`/`window.destroyed` must not call
  window methods that marshal to the platform thread (the publisher may be blocking on PLAT).
  choosing `main` delivery sidesteps this (the callback runs on the main thread, deferred).

known events: `system.ready.<name>`, `plugin.initialized.<name>`, `plugin.shutdown.<name>`,
`window.created`, `window.destroyed`. note: the renderer subscribes to **none** of them (it
polls-reconciles instead — see `reconcile_surfaces`, §8). reconcile is the right tool for
per-frame derived state with a safe convergence point; events are for sparse/awaitable signals.

**awaiting events from coroutines** (`_vent/event_bus/event_coroutine.hpp`, built on
`_vent/core/co_task.hpp`): a `co_task` (a detached, fire-and-forget coroutine) can
`co_await await_event("name")` to **suspend until the event fires, then resume** — by default
on the main thread, at the frame-start drain (resume delivery is an `event_delivery`). this is
events as a *synchronization* primitive ("run, then sync on a fired signal") rather than a
pre-registered callback, and it blocks no thread while waiting. it is layered entirely on
`subscribe` + the delivery policies: the awaiter registers a one-shot subscription whose
callback resumes the coroutine on the chosen thread. the minimal testbed demonstrates the full
path (a coroutine suspended on a worker resumes on MAIN when the event fires).

this module may see huge refactoring in the future in order to enrichen the contract. however,
the only assumptions ever done are those written in the contract. never assume any different
behavior. if the contract is not suitable for the desired behavior, change the module, test 
the module, then (and only then) redefine the contract.

<a name="94-job"></a>
### 9.4 job (`modules/job/`)

the parallelism engine: one worker thread per hardware thread (`W:00`…), global queues plus
per-worker work-stealing deques. api (`ic_job.hpp`):

- `fire(fn, priority = normal, affinity = any)` — fire-and-forget.
- `submit(fn, priority = normal, affinity = any) -> task` — returns a `task`; `task.get<T>()`
  blocks (the waiter helps execute queued work while waiting).
- `parallel_for(begin, end, fn)` — fork/join helper.
- `run_pinned_jobs()` — execute jobs pinned to the **calling** thread, then return.

**thread affinity** (`job_affinity`, the sibling of `job_priority`): `any` (default) runs on
any worker; `main` routes the job into a **main-thread inbox** that only the main thread
drains, via `run_pinned_jobs()`, which the main loop calls once per frame at frame start.
workers never steal pinned jobs, so main-affinity work is guaranteed to run on the main
thread at a known, deterministic point. this is the substrate for `event_delivery::main`
(9.3) and, later, for safely mutating main-owned state from a worker-originated request.

⚠ rules that keep it deadlock-free: a job must never block on another job unless the wait
either drains queues or uses an atomic wait the completer notifies. work submitted from a
worker goes to that worker's **local deque**, which external waiters cannot help with —
don't build cross-thread rendezvous on that. **never block-wait on a `main`-pinned job from a
non-main thread** — only the main-loop drain runs it, so the waiter would spin forever.
event-bus callbacks are jobs; everything in section 9.3's contract follows from that.

<a name="95-platform"></a>
### 9.5 platform (`modules/platform/`)

windows & the os, built on glfw. **the cleanest threading model in the engine — copy it when
in doubt**: one owning thread (**PLAT**) runs the event pump and owns all window state;
other threads reach windows via `invoke_on_platform_thread` (a marshalling queue with an
inline fast-path when already on PLAT); reads go through mutexed snapshot getters.
api: `create_window(window_desc) -> ic_window*`, `destroy_window`, `get_windows()`,
`get_main_window()`, `set_close_policy(...)` (e.g. exit when the main window closes).
⚠ window pointers can dangle after a user closes a window (PLAT deletes the object).
compare, don't dereference, unless you know the window is alive this frame. generation-checked
window handles are the planned fix.

<a name="96-asset"></a>
### 9.6 asset (`modules/asset/`)

files → engine data. paths are virtual: `protocol://relative/path`.

| mount | default target | contents |
|---|---|---|
| `vent://` | `<exe_dir>/engine_assets` | engine-owned: `shaders/default.slang.spv`, `shaders/error.slang.spv` |
| `app://` | `<exe_dir>` | the app's own files (`app://assets/model.obj`) |

defaults are established during asset-system init, anchored to the **executable's directory**
(never the cwd — apps must run from anywhere). clients may `mount()` additional or overriding
protocols. loaders: `load_model` (obj via tinyobjloader, dedup-indexed), `load_image` (stb,
rgba8), `load_shader` (spirv blob). all caches are double-checked-locked and **append-only
during a frame**; failed loads are cached as failed (no per-frame retry). ⚠ loading is
currently synchronous — first touch of an asset stalls that frame (phase 2 replaces this
with handle-based async loading; don't build new features on the raw pointers).

<a name="97-world"></a>
### 9.7 world (`modules/world/`)

deliberately boring ecs-lite (maps, not archetypes — correct at this scale): `entity` is a
`u64`; components are structs in `ic_world.hpp`:

- `transform_component { mat4 matrix }` — an entity-to-world **pose**.
- `mesh_component { model_path, texture_path }` — what to draw (string paths until phase-2
  handles).
- `camera_component { fov_y_deg, z_near, z_far }` — makes the entity a camera; the renderer
  derives view (= `inverse_rigid(pose)`) and projection (from the window's framebuffer).

api: `create_entity`, `destroy_entity`, `set_/get_` per component, `set_active_camera(e,
window=nullptr)`, `get_renderable_entities()` (cached list of mesh-bearing entities).
⚠ the world is MAIN-thread-only: mutate in the simulate phase, read-only during render.
`destroy_entity` scrubs camera assignments so windows never point at dead cameras.

**pose vs. view — the classic trap:** `math::look_at(...)` returns a **view** matrix
(world-to-eye). `math::look_at_transform(...)` returns a **pose** (camera-to-world). a
transform component stores a POSE. using `look_at` there compiles and renders wrong (double
inversion). both functions' doc comments point at each other; `mat4.hpp` also documents the
winding/handedness chain (y-flip in `perspective` ⇒ front face = clockwise ⇒ `cullMode`
choices in the vulkan pipeline).

<a name="98-renderer"></a>
### 9.8 renderer (`modules/renderer/`) — the frontend

the render-phase runnable described in section 8. owns: the frame snapshot, the string-keyed
model/texture caches (gpu handle per path), the default/error pipeline, the placeholder
texture, and handle generation (atomic counters — the *frontend* invents handles; backends
just map them). talks exclusively to `i_render_backend`
(`modules/renderer/public/renderer/interfaces/i_render_backend.hpp`) — the seam any future
backend implements: surfaces (`create/destroy/has_surface/get_surface_windows`), frame
(`begin_frame/update_frame_uniforms/end_frame`), resources (`create_/destroy_` pipeline,
mesh, texture), recording (`record_command_chunk` on workers → opaque handle →
`execute_recorded_commands`). the public `ic_renderer` is an empty marker — **clients have
no render api, by design.**

<a name="99-vulkan-backend"></a>
### 9.9 vulkan_backend (`plugins/vulkan_backend/`) — the muscle

the only place vulkan exists. key objects:

- **vulkan_backend_system**: instance/device/queues/vma allocator; surface list with
  **deferred destruction** (destroy = mark; sweep at next `begin_frame` after fence waits);
  mesh/texture maps; pipeline map; descriptor set **layouts** and the per-texture set pool;
  the thread-command-context pool for parallel recording (contexts are recycled per
  (window, frame-index) only after the fence proves the gpu finished).
- **vulkan_swapchain** (one per window): swapchain images/views, depth buffer, per-frame
  sync objects, primary command buffers, auto-recreation on resize/out-of-date — **and the
  per-window uniform ring** (see below).
- **vulkan_pipeline**: shader modules + fixed state + the two-set pipeline layout.

**descriptor set design (memorize this table):**

| set | binding | contents | update frequency | storage lives in |
|---|---|---|---|---|
| 0 | 0 | camera ubo (view+proj) | per frame | each window's swapchain (ring of `MAX_FRAMES_IN_FLIGHT`) |
| 1 | 0 | combined image sampler | per draw | each texture (allocated at `create_texture`) |

organizing sets by update frequency is the core idea of vulkan binding. recording binds set 0
when the pipeline changes and set 1 only when `packet.texture` changes — the command list is
sorted, so equal state is adjacent and binds are elided (that elision is the entire point of
sorting).

**the fence rule (the most important sentence in this handbook):** before any cpu write to
memory the gpu may read — or any resource destruction — name the fence that *proves* the gpu
is done with it. this is why frame uniforms live per-window inside the swapchain: they are
written strictly between that window's fence wait (`begin_frame`) and the submit that re-arms
it. a resource's storage must live at the scope of the fence that protects it.
`MAX_FRAMES_IN_FLIGHT` (= 2, the cpu-ahead throttle) is an editable constant in
`vulkan_backend_system.hpp` — deliberately independent of the driver-chosen swapchain image
count. invariants recording relies on: **pipelines and textures are immutable between a
frame's begin and end** (that's why workers read those maps without locks).

---

<a name="10-threading"></a>
## 10. threading model

| thread | runs | owns |
|---|---|---|
| `MAIN` | engine entry, bootstrap init, the whole frame loop (simulate + render phases), shutdown | world, frame snapshot, registry (after init) |
| `PLAT` | glfw event pump, every glfw call (marshalled), window destruction on close | all window state |
| `VLOG` | async log draining (release builds) | log sinks |
| `W:00…NN` | jobs: event callbacks, parallel system init, command recording, (phase 2:) asset io | their local deques |

the seven working rules (short form — violations are review-blockers):

1. **phase freeze.** shared structures are built, then frozen, then read from anywhere.
   mutation only at documented phase boundaries (registry after init, pipelines during a
   frame, world during render).
2. **one owner per mutable datum.** name the owning thread in the member's comment. others
   use messages, snapshots, or handles.
3. **callbacks capture owners, not stack.** nothing registered with the event bus / job
   system may capture `&local`.
4. **no job blocks on a job** unless the waiter drains queues or uses a notified atomic wait.
5. **the fence question** (section 9.9) before every gpu-visible write or destroy.
6. **handles across boundaries.** pointers don't cross ownership domains (engine↔client,
   thread↔thread, frame↔frame); generation-checked handles do. (being rolled out: assets and
   windows still use raw pointers — treat those as compare-only.)
7. **concurrency ships with its test.** new lock-free/locking code lands with a storm test.
   the containers have theirs in `source/tests/` (target `vent_tests`, tsan-capable on linux).

---

<a name="11-build"></a>
## 11. build system & sdk

- **toolchain:** gcc (mingw ucrt64 on windows), ninja, cmake presets. build with
  `cmake --build --preset windows-debug` (or `linux-debug`). run
  `build/windows/gcc/Debug/apps/minimal/minimal.exe` (needs `C:\msys64\ucrt64\bin` on PATH).
- **output layout:** `build/<platform>/<compiler>/<config>/`
  - `sdk/` — headers (`_vent/`), static module libs, plugins, templates: the redistributable.
  - `apps/<name>/` — launcher exe, `libvent_engine.dll`, `libclient_<name>.dll`, plugin dlls,
    `assets/` (app), `engine_assets/` (engine — copied by the build), `logs/`.
- **cmake functions** (in `cmake/`):
  - `vent_create_module(NAME … SOURCES … INTERNAL_DEPS …)` — static engine module.
  - `vent_create_plugin(…)` — dynamic plugin.
  - `vent_create_client(NAME … MODULES … PLUGINS … SOURCES …)` — builds engine dll (selected
    modules, whole-archive), client dll, launcher; copies plugins; **compiles and ships the
    engine shaders** into `engine_assets/shaders/`; generates `<app>_build_info.hpp`
    (startup plugin list).
  - `vent_compile_shaders(TARGET … OUTPUT_DIR … SOURCES …)` — slang → `<name>.slang.spv`
    (accepts absolute paths; entry points come from `[shader("…")]` attributes).
- **shaders** are written in slang. engine shaders live in `vent_engine/assets/shaders/`;
  app shaders (none currently) would go in `<app>/assets/shaders/` with their own
  `vent_compile_shaders` call.
- vulkan validation layers: on in debug, off in release, override with env var
  `VENT_VULKAN_VALIDATION=0/1`.

---

<a name="12-where-do-i-change-x"></a>
## 12. where do i change X? (task → file map)

| i want to… | touch these files |
|---|---|
| **add a gameplay feature to the demo** | `source/vent_apps/minimal/src/minimal.cpp` only. |
| **add a new component type** | declare in `_vent/world/ic_world.hpp` (+ getter/setter on `ic_world`); storage in `modules/world/private/world_system.hpp` + `src/world_system.cpp` (copy the camera_component pattern incl. `destroy_entity` scrub); if renderable, consume it in `renderer.cpp::extract_frame`. |
| **add a new engine system** | new module dir under `modules/<name>/` (`public/<name>/`, `private/`, `src/`, `CMakeLists.txt` with `vent_create_module`); interface `ic_<name>` in `_vent/<name>/`; class `<name>_system : system_base, i_<name>[, ir_dependencies, ir_runnable]`; `VENT_REGISTER_SYSTEM(...)` at the bottom of the .cpp; add an accessor in `_vent/accessors.hpp` + `modules/core/src/_vent/accessors.cpp`; list the module in the app's `vent_create_client(MODULES …)`. |
| **run something every frame** | implement `ir_runnable` on a system; pick a phase via `run_phase()` (`_vent/core/ir_runnable.hpp`). new phase constants go in that header. |
| **run work on the main thread from another thread** | `job()->fire(fn, priority, job_affinity::main)` — runs at the next frame-start drain. never block-wait on it from off-main. |
| **make an event callback run on the main thread** | `event()->subscribe(name, cb, event_delivery::main)` (or `::immediate` to run on the publisher's thread). default stays `::parallel`. |
| **await an event from a coroutine** | return `vent::co_task` (`_vent/core/co_task.hpp`), then `co_await vent::await_event("name")` (`_vent/event_bus/event_coroutine.hpp`); resumes on main by default. see `minimal.cpp` for the reference use. |
| **change frame order / add a phase** | `_vent/core/ir_runnable.hpp` (constants) — the sort in `main_loop.cpp::sync_runnables` needs no change. |
| **change what gets drawn / how the scene is walked** | `modules/renderer/src/renderer.cpp::extract_frame`. |
| **change per-window rendering (ubo content, submission)** | `renderer.cpp::render_window`; ubo struct in `_vent/renderer/uniform_buffer.hpp` (then also: swapchain ring size math, shader `UniformBuffer`, and `update_frame_uniforms`). |
| **add a backend capability (new gpu resource/operation)** | declare in `modules/renderer/public/renderer/interfaces/i_render_backend.hpp` → implement in `plugins/vulkan_backend/…` → drive it from the renderer frontend. never expose it in `_vent/`. |
| **change draw-call recording / binding** | `vulkan_backend_system.cpp::record_command_chunk` (mind the bind-on-change elision + the immutable-during-frame invariants). |
| **change descriptor layouts** | `vulkan_backend_system.cpp::create_descriptor_layouts` + `vulkan_pipeline.cpp` (layout) + both `.slang` shaders + (for set 0) `vulkan_swapchain.cpp::create_frame_uniforms`. |
| **change frames-in-flight** | `MAX_FRAMES_IN_FLIGHT` in `vulkan_backend_system.hpp`. one knob, everything sizes off it. |
| **add/modify an engine shader** | `vent_engine/assets/shaders/*.slang` + the explicit list in `cmake/vent_client.cmake` (engine-assets block); load via `vent://shaders/<name>.slang.spv`. |
| **add an asset loader (new format)** | `modules/asset/src/asset_system.cpp` + asset struct in `_vent/asset/`; follow the double-checked-cache + failed-flag pattern. |
| **change mounts / path resolution** | `asset_system.cpp` (`initialize` for defaults, `resolve` for syntax); exe discovery in `modules/core/src/executable_path.cpp`. |
| **create windows / react to window close** | client: `platform()->create_window(desc)`; policy via `close_policy()` override. engine-side surface lifecycle: `renderer.cpp::reconcile_surfaces` (do NOT add window event subscriptions to the renderer). |
| **add a new client app** | new dir under `vent_apps/<name>/` with `CMakeLists.txt` calling `vent_create_client`; subclass `client_base`; `VENT_REGISTER_CLIENT(my_client);` add subdirectory in `vent_apps/CMakeLists.txt`. |
| **change logging behavior** | `modules/log/` (async path) + `modules/core/private/fallback/fallback_log.hpp` (debug path) — remember both exist. |
| **touch lock-free containers** | `modules/core/public/core/containers/` (+ `modules/job/private/work_stealing_deque.hpp`) — extend the storm test in `source/tests/` first (rule 7). |
| **add/run container tests** | `source/tests/` (`test_*.cpp` + `vent_test.hpp` harness); build `--target vent_tests`, run via ctest or the exe in `build/cmake-<preset>/tests/`. tsan: configure with `-DVENT_TESTS_TSAN=ON` on linux. |
| **update the rules** | `.agents/AGENTS.md` — and this handbook if architecture facts changed. |

---

<a name="13-limitations"></a>
## 13. current limitations (honest edges)

known, accepted, and scheduled — do not "fix" these casually; they interlock with planned work:

1. **synchronous asset loading.** first touch of a model/texture stalls that frame (disk +
   parse + gpu upload with a queue drain). the next major phase replaces this with
   generation-checked asset handles, job-based loading, placeholder-backed states, and a
   staging-ring uploader. *don't build new features on raw `model_asset*` pointers.*
2. **string-keyed render caches.** every entity/frame pays two string-hash lookups; goes away
   with asset handles.
3. **window pointer lifetime.** a user-closed window's `ic_window*` dangles (PLAT deletes the
   object). engine code only compares such pointers. generation-checked window handles are the
   fix, sharing machinery with asset handles.
4. **registry container race (accepted, documented).** `_systems`/`_interfaces` may in theory
   rehash while read during parallel init with runtime plugin loads. the owner chose to keep
   the event-based init; the interim mitigation is the atomic system state + care. don't add
   runtime plugin loading paths without revisiting this.
5. **one pipeline, one material.** everything draws with the default (or error) pipeline;
   `pipeline_desc`'s entry points are effectively fixed. materials are a later phase.
6. **sort key is the entity id.** real key packing (layer|pipeline|texture|depth) is planned;
   the bind-elision that will exploit it already exists.
7. **tests: containers covered, rest bare; no ci yet.** `source/tests/` (target `vent_tests`,
   registered with ctest) storm-tests all four lock-free/lock-free-backing containers with a
   tiny self-contained harness (`vent_test.hpp`); `-DVENT_TESTS_TSAN=ON` adds ThreadSanitizer on
   the linux preset. still missing: coverage beyond the containers, and a ci runner. wiring
   `vent_tests` into ci (with the tsan build) is the standing highest-value task.
8. **standalone sdk gap:** `vent-config.cmake.in`'s `vent_create_client` (for external sdk
   users) does not yet ship `engine_assets/`; the in-workspace build does.
9. **debug builds bypass the async logger** (synchronous fallback instead) — log timing
   differs between debug and release.

---

<a name="14-glossary"></a>
## 14. glossary

| term | meaning |
|---|---|
| **entity** | a number (`u64`) identifying "a thing in the game world". has no data itself. |
| **component** | a plain data struct attached to an entity (transform, mesh, camera). |
| **world** | the container of all entities + components. the single source of truth for the scene. |
| **system** | an engine service with a lifecycle (log, platform, renderer, …). |
| **module** | a static library holding one or more systems; linked into the engine dll at build time. |
| **plugin** | a dynamic library loaded at runtime (vulkan backend, the game itself). hot-reload capable by design. |
| **role** (`ir_*`) | an opt-in capability interface of a system (runnable = ticks every frame; client = controls app lifetime). |
| **sdk** (`_vent/`) | the only headers client code may include. deliberately tiny. |
| **frontend / backend** (renderer) | frontend = api-agnostic brain (what to draw, in what order); backend = api muscle (how, in vulkan terms). |
| **handle** | a small number standing in for a resource (mesh_handle, texture_handle). safer and cheaper than pointers across boundaries. |
| **frame in flight** | a frame the cpu has recorded but the gpu hasn't finished. vent lets 2 exist at once (cpu works ahead). |
| **fence** | a gpu→cpu receipt: "i have finished this submission." the basis of all resource reuse. |
| **swapchain** | the per-window stack of images the gpu draws into and the screen presents from. |
| **descriptor set** | vulkan's "binding table" telling shaders where their buffers/textures live. vent: set 0 = per-frame camera, set 1 = per-texture. |
| **extraction / snapshot** | copying exactly what rendering needs out of the world, once per frame, so rendering never reads live game state. |
| **mount** | a virtual path prefix (`vent://`, `app://`) mapped to a real directory, exe-relative. |
| **placeholder / error asset** | the magenta checkerboard (texture) & magenta shader shown when something is missing — failure as a visible state. |
| **pose vs. view** | pose = entity-to-world ("where am i"), view = world-to-eye (its inverse). transforms store poses; the renderer derives views. |
| **marshalling** | forwarding a call to the thread that owns the data (all glfw calls run on PLAT this way). |
| **reconcile** | per-frame convergence of derived state (gpu surfaces) to the source of truth (the platform's window list) — vent's alternative to event subscriptions. |
