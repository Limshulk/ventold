# vent engine — phase 1 implementation plan (approved)

**date:** 2026-07-06
**companion to:** `20260705_1800_claude_roadmap.md` (phase 1 items 1.1–1.6),
`20260705_1800_claude_architecture_review.md` (finding ids referenced),
`20260705_1800_claude_threading_model.md` (rules R1–R7).

**goal:** invert the frame loop — the client stops driving rendering. after this phase,
`minimal.cpp` contains no `renderer()` call at all and still renders, with correct per-window
aspect and per-window cameras possible.

implementation order: **0 → 1 → 3 → 2+4 (paired) → 5 → 6 → 7.**
each step builds (`windows-debug`) and runs `minimal.exe` validation-clean before the next.

---

## step 0 — deterministic runnable ordering (groundwork, S)

**why.** once the renderer is an `ir_runnable`, two runnables exist: client and renderer. the
frame contract requires client-update-before-render (client mutates transforms; renderer reads
them). but `ir_runnable.hpp` says "called in undefined order" and `cache_role_interfaces()`
(`system_registry.cpp:617`) builds `_runnables` from an `unordered_map` — nondeterministic.
render-before-update wouldn't crash; it would render one frame stale, nondeterministically per
build. worst kind of bug.

**what & how.**
- `ir_runnable.hpp`: add `virtual auto run_phase() const -> i32 { return run_phase_simulate; }`
  plus named constants `run_phase_simulate = 0`, `run_phase_render = 1000` (gaps left for
  future phases: input, physics, extraction). lower runs earlier.
- `main_loop::sync_runnables()`: after integrating pending changes, `std::stable_sort` by
  phase. stable = same-phase runnables keep registration order → ten runs, ten identical frames.

**learn.** ordering is part of an api contract, not an emergent property. this is the seed of
tick groups / frame graphs: encoding order as sortable data means adding a physics phase later
is one line, not a loop rewrite. honors R1: phases are the freeze boundaries within a frame.

---

## step 1 — engine-owned frame orchestration (roadmap 1.1, L)

### 1a. renderer becomes a runnable

`renderer_system` additionally inherits `ir_runnable`:
- `on_update(f64)` = the engine's render tick.
- `run_phase()` returns `run_phase_render`.

wiring is free: `cache_role_interfaces()` already dynamic_casts every system to `ir_runnable`.

`renderer_system::on_update`, per frame:
1. reconcile surfaces (1b).
2. extract the world once into the command list (step 3).
3. for each platform window with a surface: `begin_frame` → update that window's camera ubo →
   record/execute chunks → `end_frame`.

### 1b. delete the renderer's duplicate window list; reconcile instead of subscribe [S-6, C-3.5]

three lists track the same windows today (platform's — the real owner, renderer's — never
rendered from, backend's surfaces). the `window.created` subscription runs on a job worker and
creates vulkan surfaces there; one innocent `set_title()` in a subscriber away from the
PLAT↔worker deadlock.

- delete `_windows`, `_windows_mutex`, `_window_sub`, `_window_destroyed_sub`, and both
  subscription lambdas (~80 lines).
- reconcile at top of `on_update`: for each `platform()->get_windows()` window without a
  surface → `create_surface` (main thread, known-safe point). surfaces whose window is gone
  from the platform list → `destroy_surface` (backend already defers via
  `marked_for_destruction`).
- new backend query: `i_render_backend::has_surface(ic_window*) -> bool`.
- caveat (comment in code): pointer comparison still touches C-5 (dangling after PLAT close) —
  no worse than today; real fix is window handles, phase 2.

**learn.** event-driven vs poll-reconcile: polling wins when the set is tiny, the reconcile
point is naturally per-frame, and it converts an async any-thread deadlock-capable callback
into deterministic main-thread code. "converge to the source of truth once per frame."

### 1c. shrink the public renderer sdk to (almost) zero

- `ic_renderer` shrinks to a marker: virtual dtor + `system_name` only. its includes of
  pipeline/render_command/texture/vertex headers leave the sdk.
- `begin_frame`/`end_frame`/`set_camera` are REMOVED (no-legacy rule), not deprecated.
- `i_renderer : public ic_renderer {}` stays as the engine-facing seam.

### 1d. frame contract documentation

update AGENTS.md in the same commit: frame = simulate phase (client `on_update`, world mutation
allowed) → render phase (world read-only, renderer extracts and submits).

**done-when:** `minimal.cpp` contains no `renderer()` call at all and still renders.

---

## step 3 — frame-level extraction, per-window replay (roadmap 1.3, M) [perf §7.4, S-7]

**why.** world walk + cache resolution + command build + sort currently happens per window
(3 windows = 3×). and recording reads live world-adjacent state — explodes the day gameplay
mutates the world from a job.

**what.** restructure `renderer_system::on_update` into named stages:

```
extract_frame()   // once per frame:
  - walk renderables, resolve model/texture caches (existing double-checked logic)
  - build _command_list, sort once
  - snapshot camera data per window BY VALUE (fov/near/far + camera transform)
render_window(w)  // per window:
  - backend begin_frame (fence wait, acquire)
  - compute view/proj from SNAPSHOTTED camera + live framebuffer size
  - update this window's ubo (step 4)
  - chunk, submit record jobs, execute, end_frame
```

snapshot = small private struct `frame_snapshot { command_list packets; window cameras }`,
rebuilt each frame. `render_packet` already copies transform by value — this makes the
boundary official. recording jobs still read backend maps under the documented
"immutable during frame" invariant — unchanged, that's the backend seam, not the world seam.

**learn.** extract → prepare → submit. a snapshot converts a data race into a memcpy; once
render reads only frozen copies, simulate N+1 can overlap render N with zero locks.

---

## step 2 — camera as a component (roadmap 1.2, M)

**why.** the camera is scene state, not a render setting. `set_camera(view, proj)` forces
clients to know linear algebra; it's per-engine, so all windows show the same stretched view.
projection from the actual framebuffer fixes aspect-on-resize free.

**what.**
- `ic_world.hpp`:
  `struct camera_component { f32 fov_y_deg = 60; f32 z_near = 0.1f; f32 z_far = 100.0f; };`
- `ic_world`: `set_camera(entity, camera_component)` / `get_camera(entity)` (third component
  map in world_system, same boring pattern), plus
  `set_active_camera(entity, ic_window* window = nullptr)` — nullptr = default for windows
  without an explicit assignment.
- renderer resolves per window: explicit → default → first camera entity → built-in fallback
  (warn once). every window renders something with zero configuration.
- math: `mat4::inverse_rigid()` — transpose 3×3 rotation, translation = -Rᵀ·t. doc the
  precondition (rigid transform, no scale/shear) and why a general inverse isn't needed.
  view = inverse_rigid(camera world transform). proj = `math::perspective` with per-window
  framebuffer aspect, 0-height guard.

**pairing note: step 2 and step 4 must land together** — different cameras per window makes
the G-1 ubo race visible; step 4 fixes it.

**learn.** retained-mode thinking: describe the scene, engine derives render state. whenever
an api takes derived data (view matrix), ask what the source data was (camera pose).

---

## step 4 — per-window per-frame uniform buffers (roadmap 1.4, M) [G-1]

**why.** one global `_global_uniform_buffers[N]` ring indexed by per-swapchain frame counters:
window B's frame-0 fence proves nothing about window A's in-flight frame 0, yet B memcpys over
ubo[0]. invisible only while all windows share one camera.

**what & how.**
- move the ubo ring into `vulkan_swapchain` (per window): MAX_FRAMES_IN_FLIGHT buffers +
  mapped pointers + descriptor sets. layout stays global on the backend (shape, not ownership).
- each swapchain gets its own small `vk::raii::DescriptorPool` → pool lifetime = surface
  lifetime, destruction automatic.
- `update_global_uniforms` → rename `update_frame_uniforms` ("global" was the bug). writes
  `_active_swapchain`'s ring slot; called from `render_window` after `begin_frame`'s fence wait
  — the write happens inside the window of time that window's own fence has proven safe.
- `record_command_chunk` binds `_active_swapchain->get_descriptor_set(current_frame)`.
- delete backend-global `_global_uniform_*` / `_global_descriptor_sets`.

**learn.** R5 — "which fence proves this memory is free?" a resource's storage must live at
the scope of the fence that protects it. global resource + per-window fences = mismatch = race.

**done-when:** two windows with different cameras render correctly under load.

---

## step 5 — engine default assets + exe-relative mounts (roadmap 1.5, M) [S-2]

**what & how.**
1. `core/utils`: `get_executable_directory() -> std::filesystem::path`
   (GetModuleFileNameW / readlink("/proc/self/exe"), one #ifdef, one function).
2. `asset_system::initialize` default mounts: `vent://` → `<exe_dir>/engine_assets`,
   `app://` → `<exe_dir>`. client `mount()` can override. delete the mount call from minimal.
3. cmake: `vent_create_client` compiles `vent_engine/assets/shaders/*.slang` and copies to
   `<app_output>/engine_assets/shaders/`.
4. promote the textured `shader.slang` to `vent_engine/assets/shaders/default.slang`;
   renderer loads `vent://shaders/default.slang.spv`. delete the app copy (no-legacy).
5. error trio (phase 2 needs all three): `error.slang` magenta shader (engine asset);
   procedural 64×64 magenta/black checkerboard texture; procedural unit cube mesh — built in
   code at renderer init, no files, no i/o, can't be missing. default shader load failure →
   fall back to error shader, log once.

**learn.** mounts are a contract about deployment ("copy the folder anywhere and run it" =
the actual meaning of sdk-based). placeholders convert failure from an absence into a visual
state — magenta is debuggable, black screens are not.

**done-when:** `minimal.exe` runs from any cwd.

---

## step 6 — texture actually per-draw (roadmap 1.6, M) [G-1.3]

**why.** `render_packet.texture` is decorative: every upload rewrites binding 1 of the global
sets (VUID violation + last-loaded-texture-wins). two entities with two textures render wrong.

**what & how.** organize descriptor sets by update frequency:
- set 0 = per-frame (camera ubo from step 4), bound once per chunk.
- set 1 = per-texture (combined image sampler), allocated at texture creation, stored in
  `vulkan_texture`, freed on destroy. backend pool sized generously (e.g. 1024) with a comment;
  growth deliberately deferred.
- `record_command_chunk` binds set 1 from `packet.texture` — only when it changes from the
  previous packet (sorted list → same textures adjacent → state-change elision). sort key is
  still entity id until phase 3.1; the elision mechanism goes in now, the key comes later.
- pipeline layout: two set layouts. `default.slang` moves texture/sampler to set 1 binding 0.

**learn.** per-frame / per-material / per-draw binding frequency — the core organizing idea of
vulkan binding, and the thing bindless later replaces. sorting is only worth its cost if
recording exploits it.

**done-when:** two entities with different textures render correctly in one frame.

---

## step 7 — the client shrinks to the north star (S)

rewrite `minimal.cpp`: create windows; spawn model entity with mesh component; spawn camera
entity with camera component + transform; `on_update` only rotates the model. deleted: mount
call, set_camera, aspect block, begin/end loop. no `renderer()` calls at all.

---

## cross-cutting

- **AGENTS.md updates (same commits, T-3):** frame-phase contract (simulate mutates, render
  reads), "pipelines immutable during frame", new mount defaults.
- **verification per step:** build windows-debug, run minimal.exe validation-clean. steps 2/4:
  two-windows-two-cameras visual check. step 6: two-entities-two-textures check.
- **explicitly NOT in phase 1:** async asset loading (phase 2), asset handles (2.1), window
  handles / C-5, init-system overhaul (stays event-based per standing note). each edge gets a
  comment pointing at its phase.
- **startup speed:** phase 1 is startup-neutral; wins live in phase 2 (async assets) and the
  config system (worker count). phase 1's contribution: shrinking shared-mutable surface so
  parallelism lands on data designed for it (review §9).
