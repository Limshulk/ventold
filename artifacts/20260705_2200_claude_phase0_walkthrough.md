# vent engine — phase 0 fixes walkthrough

**date:** 2026-07-05
**companion to:** `20260705_1800_claude_architecture_review.md` and `_roadmap.md`.
**scope:** every phase-0 task that was actionable under your roadmap notes, implemented,
built clean (`windows-debug`, gcc), and run validation-clean (exit 0, zero VUID, model +
texture + delayed window all live).

this document explains **every change**: what was wrong (and against which standard), how the
new code works, and the transferable concept to keep. it is ordered by roadmap item, and each
item ends with a short **learn** you can carry to future work.

your three constraining notes were honored:
- **0.3** kept a single **editable** frames-in-flight knob (not an un-changeable constant).
- **0.4/0.5** the init system stayed **event-based**; no topo-sort overhaul. only the safe,
  contained part of 0.4 was done.
- **0.8** `thread_registry::exit_thread()` was **kept** (only a clarifying comment added).

what was intentionally **not** done (with reasons) is in the last section.

---

## 0.8 — mechanical fixes batch

### minimal.cpp — declared dependency, null-check, real aspect ratio

**was wrong:** the client called `vent::world()` in `on_initialize` but `dependencies()` only
listed platform/renderer/asset [S-5] — it worked by luck of init order, which the parallel
init explicitly does not promise. the frame-60 "delayed window" was pushed + `show()`n with no
null check (unlike the `on_initialize` loop). the projection hardcoded `1280.0f/720.0f` even
though the main window can be resized and the delayed window is 1920×1080.

**now:** `ic_world::system_name` is in the deps array. the delayed window is only tracked/shown
if `create_window` returned non-null. the aspect ratio is derived from the main window's actual
framebuffer (`get_framebuffer_width/height`) with a 0-height guard so a minimized window can't
feed `NaN`s into the projection.

**learn:** *declared dependencies are a contract; luck is not.* and any api that can return
null (or 0) must be treated as if it will — the guard costs one branch and removes a class of
"only crashes when the user resizes/minimizes" bugs.

### work_stealing_deque::pop() — return nullopt on the lost race

**was wrong:** in the last-item case, if the owner lost the CAS to a thief, the code set
`item = T{}` and **returned that engaged optional**. for `job_t*` that's a *non-null optional
containing a null pointer*. the caller (`get_job`) would treat it as a real job, return it, and
`execute_job` would run `finish_job(nullptr)` logic / the worker would skip its global-queue
check for that iteration and sleep with work possibly available.

**now:** on a lost CAS we restore `_bottom` and `return std::nullopt` — "I got nothing", which
is the truth. the comment explains the `job_t*`-specific hazard.

**learn:** *an `optional`'s engaged/empty state must mean what the caller thinks it means.*
"empty T" and "no value" are different answers; conflating them in a lock-free structure is
exactly the kind of bug that survives for months because it only bites under steal contention.

### record_command_chunk — safe pipeline lookup; delete the dead bind path

**was wrong:** the per-packet loop did `_pipelines.at(packet.pipeline)` **three times**. `.at()`
throws `std::out_of_range` on a missing handle — on a *worker thread*. `submit_internal`
captures that exception into the task, and `task.get<void*>()` **rethrows it on the main thread
mid-frame**, where nothing catches it → `std::terminate` [G-5]. separately, the function began
by binding an `_active_pipeline` that no longer meant anything — every packet re-bound its own
pipeline right after, so the pre-bind (and the whole `bind_pipeline`/`_active_pipeline`
mechanism) was dead code [A-6].

**now:** one `_pipelines.find()` into a local `vulkan_pipeline*`; on miss we `continue` (skip
the packet) instead of throwing. `bind_pipeline` and `_active_pipeline` are deleted from the
interface (`i_render_backend`), the backend header, and the impl. a comment records the real
invariant: *pipelines are immutable between begin_frame and end_frame*, which is why reading
`_pipelines` unlocked on the worker is safe.

**learn:** *an exception thrown on a worker and rethrown on the joiner is an unhandled exception
on the joiner.* worker code should convert "not found" into data (skip/flag), not control flow
that unwinds across a thread boundary. and: delete dead abstractions — a bind API nobody's
render path uses is a trap for the next reader.

### renderer.hpp — remove the unused `i_device`

**was wrong:** `i_device* _device = nullptr;` (and its forward declaration) were never assigned
or read [A-6]. dead members imply a design that doesn't exist and mislead readers.

**now:** both gone.

### system_registry.cpp — `find()` not `operator[]`

**was wrong:** the "which plugin owns this system?" log used `_system_to_plugin[name]`, and
`operator[]` **inserts a default (empty) entry** for every non-plugin system it touches —
silently growing the map with junk — and it did so **without** the `_plugin_tracking_mutex`
that guards writes to that map from plugin-load job threads.

**now:** a `find()` under `_plugin_tracking_mutex`; nothing is inserted, and the read is
synchronized with the writers.

**learn:** *`map[key]` is a mutation.* on a lookup-only path it's both a correctness smell
(phantom entries) and, on a shared map, a data race. reach for `find()` unless you mean to
insert.

### render_command.hpp — static_assert the packet layout

**was wrong:** `render_packet` documents byte offsets but AGENTS requires a
`static_assert(sizeof)` to enforce them [A-5]; nothing caught a field being added/reordered.

**now:** `static_assert(sizeof(render_packet) == 96)`. packets are bulk-copied and chunked, so a
silent size change would be a real bug — the assert makes it a compile error.

**learn:** *if a struct's size/layout is a contract (it's memcpy'd, chunked, uploaded), assert
it.* the assert is free and turns a class of ABI/serialization bugs into build failures.

### mat4.hpp — winding documentation + size assert; cull mode restored

**was wrong:** the pipeline had been set to `cullMode = eNone` — a workaround that shades both
faces and hides winding bugs [A-10]. nothing documented *why* the winding is what it is, so the
next person would "fix" it wrong.

**now:** `frontFace = eClockwise, cullMode = eBack` in `vulkan_pipeline.cpp`, with a comment;
and a matching note in `mat4.hpp` explaining the chain: **RH z-up world → `perspective()`
negates y for vulkan clip → determinant flips handedness → on-screen winding flips → CCW world
triangles rasterize CW → declare CW as front.** plus `static_assert(sizeof(mat4) == 64)` because
the renderer memcpy's it into UBOs and push constants. verified visually: the viking room still
renders solid (culling the correct side).

**learn:** *a y-flip is a handedness flip is a winding flip.* the three are the same fact wearing
different hats; write it down once at the source (the projection) so nobody re-derives it wrong.
back-face culling isn't just a perf win — it surfaces winding bugs early instead of hiding them.

### main_loop.cpp — clamp delta_time

**was wrong:** `_delta_time` was the raw wall-clock delta. the first frame (huge), a debugger
pause, or a long stall would hand gameplay/physics a massive `dt` and teleport everything.

**now:** clamped to ≤ 0.1s (10fps floor). below that the sim slows rather than explodes.

**learn:** *never trust wall-clock dt.* every engine clamps it; the alternative is
"tunneling"/NaN physics the first time someone sets a breakpoint.

### vulkan_backend shutdown() — drain textures before the allocator dies

**was wrong:** `shutdown()` drained `_meshes` then `vmaDestroyAllocator`, but never drained
`_textures` [G-6]. a `vulkan_texture`'s `view`/`sampler` are raii, but its `VkImage` +
`VmaAllocation` are raw — freed only by `destroy_texture`. any texture surviving to shutdown
leaked its image+allocation, and once the allocator was destroyed the raw handles could never be
freed at all.

**now:** a `_textures` drain block (clear view/sampler, `vmaDestroyImage`) runs **before**
`vmaDestroyAllocator`, mirroring the mesh path. normally the frontend releases textures first;
this is the defensive backstop.

**learn:** *teardown order is a dependency graph.* a resource that lives inside an allocator must
die before the allocator. "raii cleans up automatically" is only true for the raii members — the
raw handles beside them need explicit ordering.

### cmake — third_party is private to the asset module

**was wrong:** `vent_module.cmake` added `third_party/` as a **PUBLIC** include to *every*
module [A-2], leaking stb/tinyobjloader into every module and the sdk surface — against the
"public footprint as small as possible" rule.

**now:** that global include is removed; the asset module (the only consumer) adds
`third_party/` **PRIVATE** in its own `CMakeLists.txt`.

**learn:** *include visibility is api surface.* PUBLIC means "everyone who links me inherits
this"; use PRIVATE for implementation-only dependencies so third-party headers don't become part
of your contract.

### thread_registry — document exit_thread() as load-bearing

per your note, `exit_thread()` stays. added a doxygen note that it's intentional and load-bearing
(returning from the thread lambda misbehaved on this toolchain), plus a `todo` recording the
likely root cause (static/tls destructor ordering across the dll boundary) and the known
trade-off (it skips stack unwinding, so the lambda's captures aren't destroyed).

**learn:** *a deliberate hack must be labeled as one.* the comment converts "why is there an
ExitThread here?!" (a future rabbit hole) into a known, bounded trade-off with a pointer at the
real fix.

---

## 0.6 — asset cache correctness [C-4, S-4]

**was wrong (the bug class — TOCTOU):** `load_model` / `load_image` checked the cache under the
lock, **released** the lock, did the slow load, then re-locked and did
`cache[path] = std::move(asset)` **unconditionally**. two threads racing the same path both miss,
both load, and the second assignment **destroys the first asset while the first caller still
holds a raw pointer to it** — a use-after-free. `load_shader` didn't lock at all (its
`_shader_mutex` existed but was unused — an inconsistent half-guard). the renderer's own
`_model_cache`/`_texture_cache` were read/written unlocked in `end_frame` but locked in
`shutdown()` — again, guarding one side only. and a *failed* load was retried (disk read +
error log) **every frame, per entity, per window**.

**now:**
- all three asset loaders do a **double-checked insert**: after the slow load, re-lock, re-check
  the cache, and if another thread already inserted, **drop the local copy and return the
  existing pointer**. `load_shader` now actually takes `_shader_mutex` on both the check and the
  insert.
- the renderer caches are accessed under their mutexes consistently (with a comment that these
  are render-thread-owned today, so the locks are for consistency with `shutdown()` + future
  async, not current contention).
- `cached_model`/`cached_texture` gained a `bool failed` flag; a failed load sets it and logs
  **once** ("will not retry"), so broken assets stop hammering the disk/log every frame.

**learn:** *check-then-act across a released lock is the TOCTOU bug class.* the two fixes worth
internalizing: (1) **double-check under the lock before you commit** — the second racer must
observe the first's result; (2) when the check has an expensive side effect (a load), a *failed*
result deserves to be cached too, or you'll repeat the expense forever. once you see this shape
you'll spot it everywhere — that's the point.

---

## 0.7 — event bus contract [C-3]

**was wrong:** the `subscription::valid` flag / `cleanup_invalid_subscriptions()` /
`_publish_count_after_cleanup` machinery was **dead** — `unsubscribe()` hard-erases entries, so
`valid` was never set false and the periodic cleanup scanned for a state that couldn't exist
[A-6]. worse, `_publish_count_after_cleanup++` was a non-atomic read-modify-write on a `u32`
from many job threads (formally a data race). and the bus's real concurrency guarantees were
undocumented, so every subscriber silently assumed a different contract.

**now:**
- deleted the `valid` field, `cleanup_invalid_subscriptions()`, `_publish_count_after_cleanup`,
  and `_cleanup_interval`. `dispatch`/`dispatch_wait` now snapshot **all** present callbacks
  (they're all live by construction).
- documented the contract in `ic_event_bus.hpp` doxygen: callbacks run on **job workers**, in
  **no order**, possibly **concurrently**; `unsubscribe` does **not** wait for in-flight
  callbacks; and the **platform-thread deadlock rule** — `window.created`/`window.destroyed`
  subscribers must not call window methods that marshal back to the platform thread (which
  published the event via `publish_wait`), or PLAT waits for the callback while the callback
  waits for PLAT.

(deferred: `unsubscribe_and_wait()` — not needed until the phase-2 async/shutdown paths; noted
in the header so it's not forgotten.)

**learn:** *an event bus is a scheduling primitive, not a synchronization one.* every guarantee
it does **not** make (ordering, delivery thread, unsubscribe-vs-inflight, reentrancy) is
load-bearing and must be written down, because subscribers will each assume the convenient one.
also: dead "lazy cleanup" scaffolding is worse than no cleanup — it reads as intent that isn't
real.

---

## 0.4 — atomic system state (the safe part, event-based preserved) [C-1]

**your note:** keep the init system event-based; don't overhaul. so I did **only** the contained,
non-overhaul part of 0.4 and left the event machinery intact.

**was wrong:** `system_entry::state` was a plain `enum` written from job workers
(`mark_system_ready` / `mark_system_failed` during parallel init) and read concurrently from
other workers (dependency `is_ready` checks). concurrent read/write of a non-atomic is a data
race — formally UB, and the compiler is entitled to assume it can't happen.

**now:** `state` is a `std::atomic<system_state>`. because `std::atomic` isn't movable and
`system_entry` lives in an `unordered_map` populated via `emplace(std::move(entry))`, I added an
explicit **move constructor + move assignment** that transfer the loaded value (the entry is
moved into its node exactly once, so a relaxed load/store there is fine). the `is_ready` /
`has_started` / `has_failed` queries now `.load(acquire)`; the ~6 direct read/write sites in
`system_registry.cpp` use `.load()`/`.store()` with acquire/release.

**explicitly deferred (per your note):** the *container-level* race in C-1 — `_systems` /
`_interfaces` can rehash while another thread reads them — is **not** fixed here, because the
clean fix (freeze-after-init, or a shared_mutex that would deadlock against `log()` re-entering
the registry in release) is the "overhaul" you asked to avoid. the atomic still matters: it
removes the torn-read UB on the one field that is *definitely* written and read concurrently
during parallel init. the container race remains a documented, deferred item.

**learn:** *if two threads touch a field and at least one writes, it must be atomic (or locked) —
no exceptions, even for a one-byte enum.* "x86 byte reads don't tear in practice" is not a
defense: the data race is UB at the language level, so the *compiler* can miscompile it
regardless of the hardware. and: when an atomic member blocks a container's implicit moves,
a small hand-written move that transfers the loaded value is the standard escape hatch.

---

## 0.3 — frames-in-flight, decoupled but still yours to tune [G-2]

**your note:** you want to keep experimenting with `_max_frames_in_flight`. so instead of hiding
it, I made it **one editable knob** and fixed the bug around it.

**was wrong:** three places hardcoded "3" (`create_global_uniforms`' `max_frames = 3`, the
`_pending_contexts[3]` array, and unchecked `_global_descriptor_sets[current_frame]`), while
`vulkan_swapchain::create_swapchain` **overwrote** `_max_frames_in_flight = _images.size()` —
i.e. the cpu-side frame count was silently dictated by the driver's image count. on any gpu that
returns 4+ images (mailbox commonly does), `current_frame` would reach 3 and index a 3-element
array / 3-element descriptor set **out of bounds** — memory corruption on someone else's
hardware. and the runtime `set_frames_in_flight` was half-implemented (recreate never rebuilt
sync/command/uniform resources to a new count), so it was a lie.

**now:**
- a single `static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;` in the backend header, with a comment
  saying **this is the knob** — change it to experiment. everything cpu-side sizes off it
  (`create_global_uniforms`, `_pending_contexts[MAX_FRAMES_IN_FLIGHT]`, the value passed to the
  swapchain ctor).
- `vulkan_swapchain` **no longer overwrites** `_max_frames_in_flight` with the image count. the
  cpu frame count is now independent of the driver's image count; per-image resources
  (render-finished semaphores) stay sized off `_images.size()`, which was already correct.
- removed the half-broken runtime `set_frames_in_flight` from the whole stack (`ic_renderer` →
  `i_render_backend` → `renderer` → backend → swapchain). experimentation is via the constant.

**how it holds together:** on your AMD gpu the swapchain still gets its 2–3 images; the cpu runs
`MAX_FRAMES_IN_FLIGHT = 2` frames ahead; fences/acquire-semaphores/per-frame command buffers are
sized to 2; render-finished semaphores are sized to the image count. verified validation-clean,
including the frame-60 delayed window creating its own swapchain.

**if you actually wanted *runtime* frame-count changes** (not just an editable constant), say so
— it needs `recreate()` to rebuild the per-frame sync/command/uniform resources, which is more
work than 0.3 scoped; I flagged the trade-off rather than guessing.

**learn:** *frames-in-flight and swapchain image count are different resources with different
owners.* frames-in-flight is **you** throttling the cpu (how far ahead of the gpu you let it
run); image count is the **driver** buffering presentation. conflating them — letting the driver
size your cpu-side arrays — is the single most common vulkan-tutorial trap, and it fails exactly
where you can't test it: on other people's gpus.

---

## verification performed

- `cmake --build --preset windows-debug` → **zero errors** (only pre-existing third-party +
  VMA missing-field-initializer warnings, untouched by this work).
- ran `minimal.exe` (ucrt64 on PATH, 60s cap) → **exit 0**, **zero `[E]` lines**, **zero VUID
  violations** through: boot → 3 windows → viking room model (3566 verts / 11484 idx) + 1024²
  texture → delayed 1920×1080 window at frame 60 (own swapchain created clean) → shutdown.
- visual sanity: viking room renders solid after the `eBack`/`eClockwise` cull change.
- `grep` confirms **no leftover references** to `set_frames_in_flight`, `_active_pipeline`,
  `vulkan_debug`, `registratiob`, or `cleanup_invalid`.

net diff: **+438 / −1496** across 30 files (the large deletion is mostly the three superseded
artifact docs being removed; the code net is a modest, surgical set of changes).

---

## intentionally NOT done (and why)

- **0.4 container-race / 0.5 topo-sort** — your note: keep init event-based, no overhaul.
  deferred with the atomic-state stopgap in place and the container race documented.
- **`unsubscribe_and_wait()` (0.7 item 4)** — not needed until phase-2 async; noted in the
  header so it isn't lost.
- **runtime frames-in-flight setter** — replaced by the editable constant per your experiment
  note; a real runtime setter is a larger task (offered above).
- **`thread_registry::exit_thread()` removal** — kept per your note; only documented.

these are the honest edges of this pass — each is a deliberate scope decision tied to one of your
notes, not an oversight.
