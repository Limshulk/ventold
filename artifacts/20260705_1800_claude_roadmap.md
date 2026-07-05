# vent engine — detailed roadmap

**date:** 2026-07-05
**companion to:** `20260705_1800_claude_architecture_review.md` (finding ids C-x / G-x / S-x / A-x
referenced throughout).

**north star (your words, sharpened):** a game developer with a creative background and zero
graphics/systems knowledge writes:

```cpp
auto on_initialize() -> bool override {
    vent::world()->spawn("app://models/viking_room.glb");
    return true;
}
```

…and gets a lit, textured, correctly-paced scene in a window they didn't have to create, rendered
by a backend they cannot name. every phase below is judged against that sentence.

each item has: **why → what → how → learn** (the concept you'll internalize doing it) and
**done-when** (acceptance criteria). effort tags: (S) < 1h, (M) half-day, (L) multi-day,
(XL) a week+ of evenings.

---

## phase 0 — stop the bleeding (correctness debt)

goal: the current feature set, made *actually correct*. no new features. everything here is
small, and everything here removes a landmine you'd otherwise step on mid-phase-2.

### 0.1 delete the vulkan debug-file hack (S) — [A-1]

- **what:** remove all five `fopen("C:\\dev\\vent\\build\\vulkan_debug.txt", ...)` blocks in
  `vulkan_swapchain.cpp`; delete `build/vulkan_debug.txt`.
- **done-when:** grep for `vulkan_debug` returns nothing; frame time drops measurably.
- **FIXED on 2026.07.05.**

### 0.2 single wait-idle shutdown (S) — [G-3, observed validation error]

- **what:** add `virtual auto wait_idle() -> void` to `i_render_backend`; call it once at the top
  of `renderer_system::shutdown()`; remove the per-texture `waitIdle` in `destroy_texture`.
- **learn:** gpu teardown ordering — *one* barrier at the boundary beats n scattered stalls.
- **done-when:** validation-clean shutdown with 3 windows + model + texture loaded.
- **FIXED on 2026.07.05.**

### 0.3 kill the hardcoded 3s: one MAX_FRAMES_IN_FLIGHT constant (M) — [G-2]

- **what:** `constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;` in one backend header. frames-in-flight
  becomes a cpu-side constant decoupled from image count:
  - `vulkan_swapchain`: stop overwriting `_max_frames_in_flight` with `_images.size()`;
    fences/acquire-semaphores/command buffers sized by the constant; present semaphores stay
    per-image (already correct).
  - `create_global_uniforms`, `_pending_contexts[]`: size by the constant.
  - delete `set_frames_in_flight` from the whole stack (ic_renderer → backend → swapchain) — it
    is half-implemented and now meaningless. (AGENTS: remove, don't deprecate.)
- **learn:** frames-in-flight vs swapchain-image-count are *different resources with different
  owners* — cpu throttling vs presentation buffering. most vulkan tutorials conflate them; most
  real engines use 2 + whatever image count the driver wants.
- **done-when:** runs validation-clean; forcing `minImageCount+2` in a test still works.
- **NOTE 2026-07-05**: As this is a learning project, i want to be able to modify 
  _max_frames_in_flight for experiments. 

### 0.4 registry: immutable after init + atomic state (M) — [C-1]

- **what:**
  1. make `system_entry::state` a `std::atomic<system_state>`.
  2. restructure `initialize_all` so *all* creation (`create_from_pending`, including the
     backend's) happens before any parallel init begins: split renderer init into "load plugin
     library + create systems" (main thread, before the batch) and "initialize backend" (its
     current dependency-driven place). simplest mechanical route: give the renderer a dependency
     on `i_render_backend::system_name` and have the registry pre-load plugin libraries listed by
     a new `vent_create_client` field — the backend then initializes as a normal parallel system
     and `initialize_plugin_systems`' mid-flight map mutation disappears.
  3. until 2 lands, put a `std::shared_mutex` around `_systems`/`_interfaces` as a stopgap.
- **learn:** *phase-based* thread safety — the cheapest concurrent data structure is one that is
  frozen while shared. this is the pattern (build → freeze → fan out) that scales to the whole
  engine.
- **done-when:** a boot with `--no_async` and a boot with 12 workers produce identical init
  logs (ordering aside); no map mutation happens after the first `fire_init`.
- **NOTE 2026-07-05**: Goes hand in hand with 0.5. I don't want to completely overhaul the 
  initialization system as this was one of the hardest tasks until now. It should stay event-based.

### 0.5 dependency resolution: topo-sort waves, delete the event machinery (L) — [C-2, C-3, S-3]

- **what:** replace `initialize_regular`'s subscribe/erase/fire web with:
  1. build adjacency from `pending_dependencies`; kahn's algorithm → ordered *waves* of
     independent systems; **error out with a named cycle** if one exists.
  2. for each wave: `parallel_for` the inits; failures poison dependents with a clear log.
  3. delete: dependency event subscriptions, `setup_dependency_events`, `setup_init_events`
     stubs, `event_subscriptions` on `system_entry`.
- **why not keep the event design?** it's the most complex code in the engine, holds three of the
  four worst bugs (dangling stack captures, racing vector, double-fire), gives nondeterministic
  boots, and its only theoretical advantage (mid-flight system registration) is better served by
  explicit `initialize_plugin_systems` batches.
- **learn:** kahn's algorithm; determinism as a debugging feature; "delete code as a fix" —
  the highest-leverage refactor category there is.
- **done-when:** boot order is identical across 10 runs; a deliberate a↔b dependency cycle
  produces a readable fatal log naming both systems; `system_creator.cpp` shrinks by ~150 lines.
- **NOTE 2026-07-05**: Goes hand in hand with 0.4. I don't want to completely overhaul the 
  initialization system as this was one of the hardest tasks until now. It should stay event-based.

### 0.6 asset cache correctness (M) — [C-4]

- **what:** in `load_model`/`load_image`/`load_shader`: re-check the cache under the lock before
  inserting; if an entry appeared meanwhile, discard the local load and return the cached one.
  actually use `_shader_mutex` in the shader path. renderer `_model_cache`/`_texture_cache`:
  either lock consistently in `end_frame` too, or (better) delete the renderer-side mutexes and
  document single-threaded access — pick one, don't keep half.
  add the `failed` flag so failed loads don't retry every frame [S-4].
- **learn:** check-then-act (TOCTOU) as a bug *class*; you'll start seeing it everywhere —
  that's the point.
- **done-when:** a stress test loading the same path from `parallel_for(0, 64, ...)` returns 64
  identical pointers and constructs the asset exactly once (add this as your first real test).
- **NOTE 2026-07-05**: to be worked on.

### 0.7 event bus contract (M) — [C-3]

- **what (minimum viable contract):**
  1. delete the dead `valid`/`cleanup_invalid_subscriptions` machinery — unsubscribe already
     hard-erases.
  2. make `_publish_count_after_cleanup` atomic or delete it with (1).
  3. document (in `ic_event_bus.hpp`, doxygen): callbacks run on job threads, in no order,
     possibly concurrently; `unsubscribe` does not wait for in-flight callbacks; subscribers on
     `window.created`/`window.destroyed` must not call platform-marshalling window methods
     (deadlock — review C-3.5).
  4. add `unsubscribe_and_wait()` (or make unsubscribe always drain) before phase-2 async lands —
     shutdowns will need it.
- **learn:** api contracts for concurrency: writing down what you *don't* guarantee is as
  load-bearing as the code.
- **NOTE 2026-07-05**: to be worked on.

### 0.8 small fixes batch (S each)

- `minimal.cpp`: add world to `dependencies()` [S-5]; null-check the frame-60 window; use the
  window's real framebuffer size for aspect (until S-1 removes this code entirely).
- `thread_registry`: delete `exit_thread()` and the function itself [C-6].
  - **NOTE 2026-07-05** I've tried many different things on this one - having `exit_thread()`
  was the only fix I was able to come up with after days of work.
- `work_stealing_deque::pop`: return `nullopt` on the lost race; single `#pragma once` [§8].
- `record_command_chunk`: one `_pipelines.find` into a local, skip packet on miss; delete
  `bind_pipeline`/`_active_pipeline` [G-5].
- `renderer.hpp`: delete unused `i_device* _device`; collapse the three `private:` sections.
- `system_registry.cpp:164`: `_system_to_plugin.find` not `operator[]`.
- typo `registratiob`; comment-style fixes from [A-4]; `static_assert(sizeof(render_packet))`.
- cmake: `third_party` include → private to the asset module [A-2]; move render handles out of
  `vent_sdk.hpp` into `_vent/renderer/handles.hpp` [A-3].
- decide cull mode: restore `eBack`, set `frontFace = eClockwise`, comment the handedness chain
  in `mat4.hpp` [A-10]. verify against the viking room visually.
- `main_loop`: clamp `delta_time` to e.g. 100ms.
- backend `shutdown()`: drain `_textures`/`_meshes` before `vmaDestroyAllocator` [G-6].
- **NOTE 2026-07-05**: to be worked on.

### 0.9 phase-0 exit criteria

- validation layer silent through: boot → 3 windows → model+texture → close aux window at
  runtime → exit.
- 10 consecutive boots byte-identical init order.
- `rg "todo: does this ened a mutex"` returns nothing because the question is answered in code.
- **NOTE 2026-07-05**: to be worked on.

---

## phase 1 — invert the frame loop (the sdk becomes an engine)

goal: the client stops driving rendering. this is the single biggest step toward the north star,
and it's mostly *moving* code you already have.

### 1.1 engine-owned frame orchestration (L) — [S-1]

- **what:**
  1. `renderer_system` implements `ir_runnable`. `main_loop` update order becomes:
     client `on_update` (game logic) → runnables (renderer last).
  2. renderer's `on_update`: for each platform window with a surface → begin → render → end.
     `begin_frame`/`end_frame` leave `ic_renderer` (they become engine-internal); the *public*
     renderer interface shrinks toward zero — which is exactly what AGENTS.md asks for.
  3. delete the renderer's duplicate `_windows` list; iterate `platform()->get_windows()` [S-6].
- **learn:** inversion of control — the defining difference between a *library* (client calls
  you) and an *engine* (you call the client). every plug-n-play property you admire in unreal
  falls out of owning the loop.
- **done-when:** `minimal.cpp` contains no `renderer()` call at all and still renders.

### 1.2 camera as a component (M)

- **what:** `camera_component { f32 fov_deg; f32 z_near, z_far; }` + `world()->set_camera(entity)`
  per window (default: first camera renders to all windows). renderer computes view from the
  camera entity's transform and proj from the window's *actual* framebuffer aspect each frame.
  delete `ic_renderer::set_camera`.
- **learn:** deriving render state from world state instead of pushing it imperatively — the
  first taste of "retained mode" thinking that scales to materials/lights later.
- **done-when:** two windows can show two different cameras; resizing fixes aspect live.

### 1.3 frame-level extraction, per-window replay (M) — [perf §7.4, prereq for G-1 fix]

- **what:** once per frame (not per window): walk renderables → resolve handles → build + sort
  the `command_list`. per window: update that window's camera ubo, record chunks, submit.
  this is the *extract* half of the extract/prepare/submit architecture from your gemini
  blueprint — you're already 60% there with `render_packet`.
- **learn:** extract–prepare–submit phases; why every aaa renderer snapshots scene data instead
  of reading live world state during recording (this also future-proofs world mutation from
  jobs, [S-7]).

### 1.4 per-window per-frame uniform buffers (M) — [G-1]

- **what:** move the ubo ring + descriptor sets into `vulkan_swapchain` (per window,
  `MAX_FRAMES_IN_FLIGHT` copies), written between fence-wait and submit for *that* window only.
  descriptor set layout stays global; sets become per-window.
- **learn:** resource scoping by fence domain — "which fence proves this memory is free?" is
  *the* question of gpu synchronization. if you can answer it for every buffer you write, you
  understand vulkan sync.
- **done-when:** two windows with different cameras render correctly under load
  (this is the direct test of the race in G-1).

### 1.5 engine default assets + exe-relative mounts (M) — [S-2]

- **what:** asset system mounts `vent://` → `<exe_dir>/engine_assets` (cmake copies
  `vent_engine/assets/` there) and `app://` → `<exe_dir>/assets` by default. renderer loads
  `vent://shaders/default.slang.spv`. add the classic magenta error shader + placeholder
  texture + unit-cube placeholder mesh — phase 2 needs all three.
- **done-when:** `minimal.exe` runs from any cwd; deleting the app shader shows magenta instead
  of nothing.

### 1.6 texture actually per-draw (M) — [G-1.3]

- **what:** second descriptor set (set 1, per-texture, allocated at texture creation) bound per
  packet in `record_command_chunk` from `packet.texture`. sorting by texture in the key now has
  meaning.
- **learn:** descriptor set frequency design (per-frame / per-material / per-draw) — the core
  organizing idea of vulkan binding, and the thing bindless later replaces.
- **done-when:** two entities with different textures render correctly in one frame.

---

## phase 2 — the async asset pipeline (your "async-only" principle, done right)

goal: no frame ever blocks on io or upload. this phase is where the engine starts feeling
professional, and it's the one to slow down and savor — it teaches the most.

### 2.1 asset handles + registry (L)

- **what:** `asset_handle { u32 index; u32 generation; }` (typed wrappers per asset kind).
  the asset system owns storage; handles are the only public currency; raw pointers become
  engine-internal. `mesh_component { asset_handle model; asset_handle texture; }` — resolved
  once at `set_mesh`, not per frame [perf §7.5]. states: `unloaded → loading → ready → failed`.
- **learn:** generation-checked handles — *the* game-industry answer to dangling pointers
  (same pattern will later serve windows [C-5] and entities). why value-type ids beat
  shared_ptr in engine code (cache, serialization, no lifetime spaghetti).

### 2.2 job-based loading with placeholders (L)

- **what:** `load(path)` returns a handle immediately (placeholder-backed). a job does
  io + decode; a *completion* step publishes readiness. renderer draws whatever state the handle
  is in: placeholder, real, or error-magenta. in-flight dedup: second `load` of the same path
  returns the same handle (this is where 0.6's per-key in-flight marker matures).
- **learn:** the state-machine view of async (no blocking waits anywhere); how placeholders
  convert "loading" from a *pause* into a *visual state* — the trick behind every open-world
  stream-in you've ever seen.
- **done-when:** spawning 50 viking rooms on frame 1 never drops the main loop below refresh
  rate; models pop from placeholder to real over subsequent frames.

### 2.3 upload ring + transfer batching (L)

- **what:** replace per-resource staging-buffer + `waitIdle` with: one persistent staging ring,
  per-frame transfer command buffer, uploads batched and fenced with the frame; resource ready
  only when its upload fence retires. (keep it on the graphics queue first; a dedicated transfer
  queue is a later refinement — measure before adding queue-ownership transfers.)
- **learn:** staging rings, fence-tracked "in flight until proven done" resources; why
  `waitIdle` in a frame is always a design smell.

### 2.4 deferred deletion queue (M)

- **what:** generalize the `marked_for_destruction` pattern you already invented for surfaces:
  `retire(resource, frame_fence_value)`; a per-frame sweep destroys what the gpu provably
  finished with. removes every remaining teardown `waitIdle` except final shutdown.
- **learn:** epoch-based reclamation — the same idea as hazard pointers in your deque's
  retired-arrays list, now applied to gpu memory. one concept, two domains.

### 2.5 phase-2 exit criteria

- zero `waitIdle` outside final shutdown (grep-enforced).
- cold-start scene with 100 assets: no frame > 20ms on your machine.
- kill-the-file test: deleting a texture on disk yields magenta, not a hang or spam.

---

## phase 3 — rendering depth (make the blueprint true)

the gemini blueprint's phase 2/3, now standing on safe foundations.

### 3.1 real sort keys (S) — currently the key is the entity id

pack `[layer:8 | pipeline:16 | texture:16 | depth:24]` exactly as `render_command.hpp`'s comment
promises. exploit it in recording: re-bind pipeline/descriptors *only on change* (state-change
elision is the entire payoff of sorting; today every packet re-binds).

### 3.2 parallel recording that's actually parallel (M)

with >1k packets, chunked secondary buffers start winning. profile chunk size; consider
recording per-window in parallel too (each window's primary is independent — the thread-context
pool already supports it). **measure first**: at <100 draws, single-threaded recording is
faster; keep the seq path.

### 3.3 material system v0 (L)

`material = pipeline + texture set + push-constant block`. assets reference materials, not raw
textures. this is where `pipeline_desc`'s hardcoded `vertMain/fragMain` defaults get owned by
the material compiler instead of the sdk header.

### 3.4 transform hierarchy + dirty propagation (M)

parent/child in `world`, world-matrix cache with dirty bits. gameplay-facing win, and your first
data-oriented iteration problem (update order = topological by depth).

### 3.5 frame pacing (M)

fixed-timestep option for logic, frame limiter when unfocused/minimized, delta smoothing.
"the user never thinks about frame order" includes *time* behaving sanely.

---

## phase 4 — the plug-n-play shell

the phase that makes vent feel like a product. items here are intentionally coarser — re-plan
when you arrive with phase 1–3 experience.

- **4.1 input module** (glfw callbacks → action mapping; `input()->pressed("jump")`) — the next
  system a creative user needs after "i can see things".
- **4.2 config system** (`vent.toml`: window size, worker count [perf §7.8], validation toggle,
  mounts) — kills a whole todo category.
- **4.3 default window** — client declares `window_desc` in a struct (or config), engine creates
  it before `on_initialize`; `platform()` disappears from the beginner path. the north-star
  snippet becomes literally true.
- **4.4 hot-reloadable game plugin** — the architecture was *built* for this (client is already
  a plugin). needs: 0.5's deterministic init, C-2's fix (gone with 0.5), 0.7's unsubscribe
  draining, and asset handles (2.1) so reload survives pointer identity. deliver as: watch
  `libclient_*.dll` timestamp → drain jobs → shutdown client system → reload → re-init.
  the dopamine payoff of the whole roadmap.
- **4.5 scene description** (json/toml scene: entities, components, asset paths) + `spawn()`
  api — creative users assemble instead of program.
- **4.6 minimal in-engine ui** (stats overlay first: fps, frame graph, job utilization — you
  have the data already; dear imgui as an `imgui_overlay` module is the pragmatic call and the
  cmake `OPTIONAL_MODULES` hook for it already exists).
- **4.7 memory system** — the `// todo: custom allocator` scattered everywhere: frame arenas
  first (linear allocator reset per frame — trivial and immediately useful for extraction/
  command lists), pools second (job_t [perf §7.7]), tracking third. do it *after* phase 2 so you
  know your real allocation patterns.

---

## continuous tracks (start now, never stop)

### T-1 testing — the engine has zero tests and three hand-rolled lock-free structures

that combination is how lock-free code stays wrong for years. minimum viable rig (S–M):

- a `vent_tests` target (catch2 or doctest, fetched like glfw).
- first four suites, in this order of value: `mpmc_queue` (n producers × m consumers, count
  conservation), `work_stealing_deque` (owner pop vs thief steal storm — this would have caught
  the engaged-nullopt bug), asset-cache race (0.6's done-when), topo-sort waves (cycle → error).
- **run them under `-fsanitize=thread` on the linux preset.** tsan on the wsl/linux build is the
  single highest-value tool available to this codebase — it would have flagged C-1, C-3.4, C-4,
  and the world/renderer race *mechanically*. add a `linux-tsan` cmake preset.
- **learn:** property-style concurrency testing (invariants under storm, not example-based).

### T-2 ci (M)

github actions: linux-debug + windows-debug build, tests, tsan job. a red X on a broken push is
worth more than any review document, this one included.

### T-3 agents.md hygiene

- phase 0.5 and 1.1 change init and frame contracts — update AGENTS.md in the same commits
  (it demands this of you in its own header).
- add the two contracts this review found undocumented: event-bus guarantees (0.7),
  "pipelines immutable during frame" (G-5), thread-ownership table (see threading doc).

### T-4 profiling honesty

before phase 3's "parallel recording" claims: get real numbers. tracy integrates in an afternoon,
understands fibers/jobs, and will show you the spin-wait in `wait_for_state` [perf §7.3] as a
glowing red bar. adopt the rule: **no parallelism pr without a before/after capture.**

---

## suggested sequencing (evenings & weekends realism)

```
week 1      phase 0.1–0.3, 0.8 (the small-fix batch is one satisfying evening)
week 2      phase 0.4 + 0.6 + 0.7, start T-1 (queue tests + tsan preset)
week 3–4    phase 0.5 (topo waves) — the big deletion. celebrate it.
week 5–6    phase 1.1–1.3 (loop inversion, camera, extraction)
week 7      phase 1.4–1.6 (per-window ubos, default assets, per-draw textures)
week 8+     phase 2, one item at a time — 2.1 handles first, everything leans on it
            phase 3/4 re-planned when you get there; 4.4 (hot reload) as the reward
```

rule of thumb baked into this ordering: **never build a new async feature on a foundation with a
known data race.** phase 0 is short precisely so that rule costs you almost nothing.

---

## what NOT to do (anti-roadmap)

explicit non-goals for the next few months, to protect the learning path:

- **no archetype/chunk ecs rewrite.** the map-based world is fine below ~10k entities; you'd be
  optimizing a bottleneck you don't have while phase-2 concepts wait.
- **no render graph yet.** with one pass, a render graph is pure ceremony. earn it with shadows
  + post-processing first.
- **no bindless / descriptor indexing** until per-draw sets (1.6) have taught you the problem
  bindless solves.
- **no dx12 backend** to "prove the abstraction" — the frontend/backend seam is already proven
  by the api surface; a second backend is a maintenance tax with no learning delta until the
  renderer is stable.
- **no custom allocators before phase-2 measurements** (except the trivial frame arena).
- **resist adding systems (audio, networking, physics)** before phase 1 lands — every new system
  multiplies the surface of the init/threading model you're about to fix.
