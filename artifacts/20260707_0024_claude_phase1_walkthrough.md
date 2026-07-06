# vent engine — phase 1 walkthrough: inverting the frame loop

**date:** 2026-07-07
**companion to:** `20260706_2352_claude_phase1_plan.md` (the approved plan),
`20260705_1800_claude_roadmap.md` (phase 1 items 1.1–1.6).
**scope:** every phase-1 change, implemented, built clean (`windows-debug`, gcc), and run
validation-clean three times — including a two-camera/two-texture stress variant and a launch
from `C:\` as working directory.

this document explains **every change**: what was wrong (and why it mattered), how the new code
works line-by-line where it counts, and the transferable concept. each section ends with a
**learn** block. read it top to bottom once; afterwards it works as a reference for "why is this
code shaped like this?".

the one-sentence summary of phase 1: **the client stopped being the render programmer.**
before, `minimal.cpp` computed matrices and drove `begin_frame`/`end_frame` per window. now it
describes a scene — windows, a mesh entity, a camera entity — and the engine does everything
else. that flip (inversion of control) is the defining difference between a *library* (client
calls you) and an *engine* (you call the client).

---

## table of contents

1. [step 0 — frame phases: deterministic runnable ordering](#step-0)
2. [step 1 — the renderer becomes a runnable](#step-1)
3. [step 1b — reconcile instead of subscribe](#step-1b)
4. [step 1c — the public renderer interface shrinks to zero](#step-1c)
5. [step 3 — extract once, render per window](#step-3)
6. [step 2 — the camera is a component (and the pose/view trap)](#step-2)
7. [step 4 — per-window uniform rings: the fence question](#step-4)
8. [step 6 — per-draw textures: descriptor frequency design](#step-6)
9. [step 5 — exe-relative mounts and engine-owned assets](#step-5)
10. [step 7 — the client, after](#step-7)
11. [verification performed](#verification)
12. [intentionally NOT done](#not-done)

---

<a name="step-0"></a>
## step 0 — frame phases: deterministic runnable ordering

**files:** `_vent/core/ir_runnable.hpp`, `modules/core/src/main_loop.cpp`

**was wrong:** `main_loop` updated its runnables in whatever order
`system_registry::cache_role_interfaces()` produced — and that function iterates an
`unordered_map`, whose order is an accident of hashing. with only the client registered as a
runnable this was invisible. the moment the renderer became a second runnable, order started to
*matter*: the client mutates transforms, the renderer reads them. render-before-update wouldn't
crash — it would render one frame stale, **nondeterministically per build**, per standard-library
version, per hash seed. bugs like that surface as "the camera feels laggy on my machine only".

**now:** `ir_runnable` gained a phase hook:

```cpp
inline constexpr i32 run_phase_simulate = 0;     // gameplay. world mutation allowed.
inline constexpr i32 run_phase_render   = 1000;  // world is read-only by contract.

virtual auto run_phase() const -> i32 { return run_phase_simulate; }
```

and `main_loop::sync_runnables()` finishes with:

```cpp
std::stable_sort(_runnables.begin(), _runnables.end(),
                 [](const ir_runnable* a, const ir_runnable* b) {
                     return a->run_phase() < b->run_phase();
                 });
```

three deliberate details:

1. **`stable_sort`, not `sort`.** within the same phase, registration order is preserved. that
   makes the frame order *fully* deterministic: ten runs, ten identical frames. `std::sort` on
   equal keys may shuffle — determinism would leak away exactly where you stopped looking.
2. **gaps in the constants** (`0`, `1000`). a future `run_phase_input = -1000` or
   `run_phase_physics = 500` slots in without renumbering anything.
3. **the sort lives in `sync_runnables()`**, which already runs at every frame start to
   integrate pending add/removes — so a runnable added at runtime (a hot-loaded plugin, say) is
   ordered correctly on its very first frame, with no extra code path.

**learn:** *ordering is part of an api contract, not an emergent property.* when order matters,
encode it as **sortable data** rather than hardcoding "call a, then b" in the loop — that's the
seed of every tick-group/frame-graph system in unreal/unity. and remember `stable_sort` is the
determinism-preserving sort; reach for it whenever equal keys have a meaningful prior order.

---

<a name="step-1"></a>
## step 1 — the renderer becomes a runnable

**files:** `modules/renderer/private/renderer.hpp`, `modules/renderer/src/renderer.cpp`

**was wrong:** the renderer was a passive service. `ic_renderer` exposed
`begin_frame`/`end_frame`/`set_camera`, and the *client* orchestrated the frame — remembering
the begin/end pairing discipline, iterating windows, owning the camera math. AGENTS.md says
"the client developer NEVER has to even look into renderer modules"; the code said the opposite.

**now:** `renderer_system` additionally inherits `ir_runnable`:

```cpp
auto on_update(f64 delta_time) -> void override;   // the engine's render tick.
auto run_phase() const -> i32 override { return run_phase_render; }
```

and the tick body is four named stages:

```cpp
auto renderer_system::on_update(f64) -> void {
    reconcile_surfaces();        // converge surfaces to the platform's window list.
    ensure_default_resources();  // lazy: default pipeline + placeholder texture.
    extract_frame();             // world -> command list + camera snapshots. ONCE.
    for (const auto& wc : _window_cameras) {
        render_window(wc);       // per window: begin, ubo, record, submit, present.
    }
}
```

the beautiful part: **wiring cost zero lines.** `cache_role_interfaces()` already
`dynamic_cast`s every system to `ir_runnable*` and hands the list to the main loop — the
mechanism existed since the runnable role was designed, it had just never been used by an
engine-side system. the client (via `client_base`, which is also an `ir_runnable` at
`run_phase_simulate`) runs first; the renderer runs second. that IS the frame now.

**learn:** *inversion of control is a responsibility move, not new machinery.* when an
architecture is right, big features land by **relocating** code, not writing it. if a "big"
feature needs lots of new scaffolding, the scaffolding was probably missing for the last three
features too — build it once, as a role/interface, and later features become free.

---

<a name="step-1b"></a>
## step 1b — reconcile instead of subscribe

**files:** `modules/renderer/src/renderer.cpp` (`reconcile_surfaces`),
`modules/renderer/public/renderer/interfaces/i_render_backend.hpp` (`has_surface`,
`get_surface_windows`), vulkan backend implementations of both.

**was wrong:** three lists tracked the same windows: the platform's `_windows` (the real owner),
the renderer's own `_windows` vector + mutex + two event subscriptions (never actually rendered
from!), and the backend's `_surfaces`. worse than the redundancy was *where the event callbacks
ran*: every event-bus callback executes on a **job worker thread**. so vulkan surfaces were
created on random workers, concurrently with whatever else was in flight — and one innocent
`window->set_title(...)` inside such a callback away from the platform-thread marshalling
deadlock (the callback waits for PLAT, PLAT waits for the callback).

**now:** both subscriptions are deleted (~80 lines gone) and replaced with a per-frame
convergence pass at a known-safe point — frame start, main thread:

```cpp
auto renderer_system::reconcile_surfaces() -> void {
    auto windows = platform()->get_windows();
    for (auto* w : windows) {                       // creation:
        if (w && !_backend->has_surface(w)) {       //   window without surface -> create.
            _backend->create_surface(w);
        }
    }
    for (auto* sw : _backend->get_surface_windows()) {  // destruction:
        if (std::find(windows.begin(), windows.end(), sw) == windows.end()) {
            _backend->destroy_surface(sw);          //   surface without window -> destroy
        }                                           //   (deferred inside the backend).
    }
}
```

subtleties worth knowing:

- `destroy_surface` only **marks**; the backend sweeps marked surfaces at the next
  `begin_frame`, after waiting that swapchain's fences. deferred destruction was already the
  house pattern for surfaces — reconcile plugs into it unchanged.
- the destruction loop compares window pointers but **never dereferences** them — the window may
  already be dead (user clicked X; the platform thread destroys it). that's exactly as safe as
  the old event path, no better, no worse; *real* lifetime safety needs generation-checked
  window handles (phase 2 material, review C-5).
- windows created before the renderer even initialized are picked up by the first reconcile —
  the old code needed a separate "check for existing windows" block at init to cover that case.
  reconcile covers startup windows, runtime windows (minimal's frame-60 delayed window), and
  closed windows with the same six lines.

**learn:** *event-driven and poll-reconcile are the two ways to keep derived state in sync, and
polling wins more often than it feels like it should.* it wins when (a) the set is small, (b) a
natural per-frame safe point exists, and (c) the event path would run on a dangerous thread.
"converge to the source of truth once per frame" is the same idea react's render loop and every
ecs system iteration are built on. reserve events for things that are genuinely sparse and
latency-critical. bonus lesson: **deleting code is a fix** — this one removed a nondeterminism
source and a deadlock class in the same commit.

---

<a name="step-1c"></a>
## step 1c — the public renderer interface shrinks to zero

**file:** `_vent/renderer/ic_renderer.hpp`

**was wrong:** the client-facing `ic_renderer` carried `begin_frame`, `end_frame`, `set_camera`,
and dragged four sdk headers (pipeline_desc, render_command, texture_desc, vertex) into every
client translation unit. every one of those was an invitation for client code to become render
code.

**now:** the whole interface is

```cpp
class ic_renderer {
public:
    virtual ~ic_renderer() = default;
    static constexpr std::string_view system_name = "vent.system.renderer";
};
```

a *marker*. the only reason a client touches this header is to declare the renderer as a
dependency by name. `begin_frame`/`end_frame`/`set_camera` were **removed**, not deprecated —
AGENTS.md's no-legacy rule: there has never been a project using vent, so old code gets deleted,
and anything that breaks gets fixed (minimal.cpp did, step 7).

**learn:** *an interface that shrinks as a system matures is the sign the abstraction is
working.* the sdk's job is to let clients express **intent** ("this entity has a mesh", "this
entity is the camera"); everything between intent and pixels is the engine's business. measure
your public surface in "things a beginner can misuse" — this change took that number to zero
for rendering.

---

<a name="step-3"></a>
## step 3 — extract once, render per window

**files:** `modules/renderer/src/renderer.cpp` (`extract_frame`, `render_window`),
`modules/renderer/private/renderer.hpp` (`frame_snapshot` members)

**was wrong:** the old `end_frame(window)` did *everything* — walk the world, resolve string
caches, build the command list, sort it, record, submit — **per window**. three windows meant
3× the identical main-thread work for byte-identical packet lists. architecturally worse: code
running during rendering read *live* world state, which is exactly the thing that explodes the
day gameplay mutates the world from a job while a frame is in flight.

**now:** extraction happens once, into renderer-owned snapshot state:

```cpp
// extraction output: everything the per-window render stage touches is
// copied BY VALUE here, once per frame.
struct window_camera {
    ic_window*       window;   // identity until render_window dereferences it.
    math::mat4       pose;     // camera-to-world, copied from the camera entity.
    camera_component camera;   // fov / near / far, copied.
};
command_list               _command_list;    // packets: handles + matrices, by value.
std::vector<window_camera> _window_cameras;
```

`extract_frame()` walks `world()->get_renderable_entities()` exactly once, resolves the
model/texture caches (unchanged double-checked locking from phase 0), pushes `render_packet`s
(96 bytes each, `static_assert`ed — they carry the transform *by value*), sorts once, then
snapshots each surface-bearing window's camera. `render_window(wc)` afterwards touches **only**
`_command_list` and `wc` — never `world()`.

per-window cost is now: fence wait, one ubo memcpy, job submission for recording, submit,
present. the O(entities) work happens once regardless of window count.

note what this deliberately does *not* fix: recording jobs still read the backend's
`_meshes`/`_pipelines`/`_textures` maps under the documented "immutable during a frame"
invariant. that is the *backend* seam and it's fine; extraction is about the **world** seam.

**learn:** *extract → prepare → submit is the architecture of every serious renderer* (frostbite,
destiny, unreal's render proxies). the core insight is brutally simple: **a snapshot converts a
data race into a memcpy.** once the render stage provably reads only frozen copies, simulation
for frame N+1 can overlap rendering of frame N with zero locks — that's the actual road to "use
every core", and it starts with a boring-looking hoist out of a loop.

---

<a name="step-2"></a>
## step 2 — the camera is a component (and the pose/view trap)

**files:** `_vent/world/ic_world.hpp`, `modules/world/*` (component storage),
`_vent/math/mat4.hpp` (`inverse_rigid`, `look_at_transform`)

**was wrong:** the camera was a *render setting* — `set_camera(view, proj)`, one per engine.
consequences: every client had to know view/projection math; all windows shared one camera (the
1920×1080 delayed window rendered stretched with a hardcoded 1280/720 aspect); and "camera" was
invisible to everything that reads the world, because it wasn't *in* the world.

**now:** three pieces.

**1. the component** — scene data, not matrices:

```cpp
struct camera_component {
    f32 fov_y_deg = 60.0f;   // vertical field of view in degrees.
    f32 z_near    = 0.1f;
    f32 z_far     = 100.0f;
};
```

no view matrix, no aspect. the *pose* comes from the entity's `transform_component` like any
other entity; the *aspect* comes from each window's actual framebuffer, per frame — which is why
resizing a window now fixes its aspect live, for free.

**2. assignment + resolution** — `world()->set_active_camera(entity, window = nullptr)`;
`nullptr` sets the default. resolution order (in `world_system::get_active_camera`):
explicit per-window assignment → default camera → **first entity with a camera component** →
`INVALID_ENTITY` (renderer falls back to a built-in pose and warns *once*). two details that
matter:

- "first camera" is tracked in a `_camera_entities` **vector in creation order** — resolving
  from the `unordered_map` would reintroduce nondeterminism through the back door.
- every step re-validates that the entity still *has* a camera component, and
  `destroy_entity` scrubs the default and all per-window assignments. a stale assignment
  degrades to the next resolution step instead of rendering garbage from a dead entity.

**3. the math — pose vs. view.** this deserves its own paragraph because it bit us *during this
very phase* (see step 7). a transform component stores an **entity-to-world** matrix (a pose:
"where is this thing?"). a view matrix is the exact opposite: **world-to-eye** ("move the world
so the camera sits at the origin"). they are inverses. so:

- clients pose cameras with the new `math::look_at_transform(eye, center, up)` — basis vectors
  as columns, eye as translation. a pose.
- the renderer derives `view = math::inverse_rigid(pose)` each frame.
- `inverse_rigid` is ~15 lines instead of a ~60-line cofactor inverse because camera transforms
  are **rigid** (orthonormal rotation + translation): the inverse of a rotation is its
  transpose, so `M⁻¹ = [Rᵀ | -Rᵀ·t]`. the function is named `inverse_RIGID` precisely because
  feeding it a scaled matrix produces silent garbage — the precondition is in the name and the
  doc comment.

**learn:** two lessons. first, *retained-mode thinking*: describe the scene, let the engine
derive render state — whenever an api takes derived data (a view matrix), ask what the source
data was (a camera pose) and take that instead; materials and lights will follow the same
pattern later. second, *a pose and a view matrix are inverses wearing different hats* — apis
must make the direction unmistakable (`look_at` returns view, `look_at_transform` returns pose),
because the compiler cannot tell a mat4 from a mat4.

---

<a name="step-4"></a>
## step 4 — per-window uniform rings: the fence question

**files:** `plugins/vulkan_backend/private/vulkan_swapchain.hpp` / `src/vulkan_swapchain.cpp`
(`create_frame_uniforms`, `write_frame_uniforms`), backend header/impl
(`update_frame_uniforms`, layout split)

**was wrong (the worst latent bug in the codebase, review G-1):** the backend owned ONE array of
uniform buffers (`_global_uniform_buffers[frames_in_flight]`), but the index used to write it
was each swapchain's **own** frame counter. window A submits gpu work reading slot 0; window B —
whose own slot-0 fence signaled long ago — happily memcpys new camera data into slot 0 **while
A's submission is still executing on the gpu**. B's fence proves nothing about A's work. the
race was invisible only because all windows shared one camera and the racing writes wrote
identical bytes. step 2 gave windows different cameras — so step 4 had to land in the same
change, or we'd have converted a latent race into flickering matrices.

**now:** the uniform ring moved *into* `vulkan_swapchain` — the object whose fences actually
protect it. each swapchain owns:

```cpp
std::vector<VkBuffer>                _uniform_buffers;       // MAX_FRAMES_IN_FLIGHT of them,
std::vector<VmaAllocation>           _uniform_allocations;   // persistently mapped.
std::vector<void*>                   _uniform_mapped;
vk::raii::DescriptorPool             _descriptor_pool;       // per-swapchain pool!
std::vector<vk::raii::DescriptorSet> _frame_descriptor_sets; // set 0, per frame index.
```

and the write path is:

```cpp
// renderer_system::render_window:
if (!_backend->begin_frame(wc.window)) return;   // <- waits THIS window's frame fence.
_backend->update_frame_uniforms(ubo);            // -> _active_swapchain->write_frame_uniforms
// ... record, submit (signals that same fence) ...
```

`write_frame_uniforms` memcpys into `_uniform_mapped[_current_frame]` — and the reason that is
correct is *positional*: it only ever runs between the fence wait for `_current_frame` and the
submit that re-arms it. the fence that proves the slot free and the fence that protects the
window are now **the same fence**, by construction.

two supporting decisions:

- **per-swapchain descriptor pool.** pool lifetime = surface lifetime, so when a window closes
  its sets die with the pool automatically — no cross-object bookkeeping, no dangling sets.
  pools are cheap; "one pool per lifetime domain" is a very good default rule.
- **the layout stays on the backend.** a descriptor set *layout* describes shape, not
  ownership — one layout serves every window's private sets and every pipeline. (the swapchain
  constructor takes it as a parameter.)

the rename `update_global_uniforms → update_frame_uniforms` is not cosmetic: "global" was the
*bug*, and names that promise the wrong scope get code written against the wrong scope.

**learn:** *"which fence proves this memory is free?" is the single organizing question of gpu
synchronization.* the generalizable rule: **a resource's storage must live at the scope of the
fence that protects it.** global storage + per-window fences = scope mismatch = race, no matter
how careful the code around it is. when you find yourself asking "do i need more
synchronization here?", first ask "is this data at the wrong scope?" — moving it is usually the
fix that needs *less* synchronization, not more.

---

<a name="step-6"></a>
## step 6 — per-draw textures: descriptor frequency design

**files:** backend header (`vulkan_texture::descriptor_set`, texture layout + pool),
`create_texture`, `record_command_chunk`, `vulkan_pipeline.cpp` (two-set layout),
`assets/shaders/default.slang`

**was wrong:** `render_packet.texture` was decorative. every `create_texture` wrote binding 1 of
the *global* descriptor sets, so (a) the whole scene sampled whichever texture uploaded **last**
— a two-textured scene was silently wrong; and (b) it updated descriptor sets that in-flight
command buffers referenced, a validation violation that only passed because the single texture
uploaded on frame one before anything was in flight.

**now — the frequency split.** descriptor sets are organized by *how often their contents
change*, which is the core organizing idea of vulkan binding:

| set | contents | changes | storage | bound |
|-----|----------|---------|---------|-------|
| 0 | camera ubo | per frame | per **window** (swapchain ring) | when pipeline changes |
| 1 | combined image sampler | per draw | per **texture** (allocated at `create_texture`) | when packet.texture changes |

`create_texture` now allocates the texture's own set from a backend pool (1024 sets, growth
deliberately deferred until phase-2 streaming shows real numbers) and writes it once. a freshly
allocated set references nothing, so writing it is **always** legal — the old vuid hazard is
structurally impossible now, not just avoided.

`record_command_chunk` exploits the sorted packet list with bind-on-change elision:

```cpp
vulkan_pipeline* bound_pipeline = nullptr;
texture_handle   bound_texture  = INVALID_TEXTURE_HANDLE;

for (const auto& packet : chunk) {
    // ... resolve pipeline / mesh / texture_set, skip-on-miss ...
    if (pipeline != bound_pipeline) {
        cmd->bindPipeline(...);
        cmd->bindDescriptorSets(..., 0, {frame_set}, {});   // set 0 rides with the layout.
        bound_pipeline = pipeline;
        bound_texture  = INVALID_TEXTURE_HANDLE;   // layout change invalidates set 1.
    }
    if (packet.texture != bound_texture) {
        cmd->bindDescriptorSets(..., 1, {texture_set}, {});
        bound_texture = packet.texture;
    }
    // vertex buffers, push constants, draw ...
}
```

three things to internalize in that snippet:

- **sorting is only worth its cost if recording exploits it.** the command list was already
  sorted; until now every packet re-bound everything, making the sort pure ceremony. adjacency
  of equal state is the entire product of sorting — elision is how you *spend* it.
- the `bound_texture = INVALID` reset on pipeline change: binding a new pipeline layout can
  disturb set compatibility, so the conservative reset keeps the elision *correct*, not just
  fast. (the sort key is still the raw entity id — real key packing
  `[layer|pipeline|texture|depth]` is roadmap 3.1; the elision mechanism is now pre-wired for
  it.)
- the initial `nullptr`/`INVALID` values double as the "secondary command buffers inherit no
  bindings" guarantee — state is always established once per chunk.

the shader moved its sampler to `set 1, binding 0` (`[[vk::binding(0, 1)]]` in slang), and
`vulkan_pipeline` builds its layout from **both** set layouts. the error shader shares the same
two-set layout while ignoring set 1 — binding an unused set is legal, which is what makes the
two pipelines hot-swappable.

**learn:** *per-frame / per-material / per-draw frequency is THE mental model for vulkan
descriptors* — and it's also the concept that "bindless" later replaces, so you can't appreciate
descriptor indexing until you've maintained frequency-sorted sets by hand. secondary lesson:
prefer designs where the dangerous operation is *impossible* (write-once fresh sets) over
designs where it's merely avoided (update shared sets "carefully").

---

<a name="step-5"></a>
## step 5 — exe-relative mounts and engine-owned assets

**files:** `modules/core/public/core/utils/executable_path.hpp` + `src/executable_path.cpp`,
`modules/asset/src/asset_system.cpp` (default mounts), `cmake/vent_client.cmake` +
`cmake/vent_shader.cmake` (engine asset shipping), `vent_engine/assets/shaders/default.slang` +
`error.slang`, renderer (`ensure_default_resources`)

**was wrong, twice:** the renderer loaded `"app://assets/shaders/shader.slang.spv"` — an engine
module reaching into the *app's* asset space; any app not shipping that exact file rendered
nothing. and `app://` was mounted to `"."` — the current working directory — so the app only
worked when launched from its own folder (double-clicking it in explorer: black screen).

**now, four pieces:**

1. **`get_executable_directory()`** — one function, one `#ifdef`: `GetModuleFileNameW(nullptr,…)`
   on windows (wide api on purpose — the ansi variant mangles non-ascii user paths),
   `readlink("/proc/self/exe")` on linux (which does *not* null-terminate — the length is used
   explicitly). this is exactly the platform dirt the core module exists to bury.
2. **default mounts** in `asset_system::initialize`:
   `vent:// → <exe_dir>/engine_assets`, `app:// → <exe_dir>`. clients can still `mount()` over
   them; minimal's old `mount("app://", ".")` was deleted — it would have silently *reintroduced*
   the cwd dependency by overriding the good default.
3. **the engine ships its own shaders.** `vent_create_client` compiles
   `vent_engine/assets/shaders/{default,error}.slang` into `<app>/engine_assets/shaders/`
   (`vent_compile_shaders` learned absolute source paths for this). the app's `shader.slang`
   was *promoted* to the engine's `default.slang`; the unused `dummy.slang` was deleted
   (AGENTS: remove unused code).
4. **failure is a visual state.** `ensure_default_resources()` loads the default shader; if
   missing, it falls back to `error.slang` — solid magenta — and logs once. it also generates a
   64×64 magenta/black checkerboard texture **in code** (no file, no i/o, cannot be missing),
   which `extract_frame` substitutes for any texture that failed to load. an entity with a
   broken texture path now renders checkerboarded instead of disappearing. phase 2 reuses the
   same placeholder as the "still loading" visual.

**learn:** *mounts are a contract about deployment.* "copy the folder anywhere and run it" is
the operational meaning of "sdk-based", and it only holds if every path resolves relative to the
executable, never the cwd. and: *placeholders convert failure from an absence into a state.*
the magenta checkerboard is a forty-year industry tradition because a wrong-but-visible frame
localizes the bug to "that one texture", while a black screen localizes it to "somewhere in the
engine".

---

<a name="step-7"></a>
## step 7 — the client, after

**file:** `vent_apps/minimal/src/minimal.cpp`

the whole render responsibility of the client is now:

```cpp
// on_initialize: describe the scene.
_model_entity = world()->create_entity();
world()->set_mesh(_model_entity, {.model_path   = "app://assets/viking_room.obj",
                                  .texture_path = "app://assets/viking_room.png"});

_camera_entity = world()->create_entity();
world()->set_camera(_camera_entity, {.fov_y_deg = 45.0f, .z_near = 0.1f, .z_far = 100.0f});
world()->set_transform(_camera_entity,
    {.matrix = math::look_at_transform({2,2,2}, {0,0,0}, {0,0,1})});
world()->set_active_camera(_camera_entity);

// on_update: gameplay only.
world()->set_transform(_model_entity,
    {.matrix = math::rotate_z(_elapsed * math::radians(90.0f))});
```

deleted from the client: the asset mount, `set_camera(view, proj)`, the aspect-ratio
computation, the whole per-window `begin_frame`/`end_frame` loop. `grep renderer minimal.cpp`
finds nothing — the roadmap's done-when, literally.

**a bug worth remembering:** an intermediate version of this file set the camera's transform
with `look_at(...)`. that compiles perfectly and renders wrong: `look_at` returns a **view**
matrix, but a transform component stores a **pose**, and the renderer inverts the pose to get
the view — so the result was a double inversion (the camera sitting at the world origin looking
the wrong way, roughly). the fix is `look_at_transform`. this is the pose/view trap from step 2
biting in practice within hours of the api existing — which is exactly why the two functions
have loudly different names and doc comments pointing at each other.

**learn:** the north-star test for engine ergonomics is *what the smallest client looks like*.
every line you delete from the minimal client is a line every future user never has to write —
or get wrong.

---

<a name="verification"></a>
## verification performed

- `cmake --build --preset windows-debug` → **zero errors** across all steps (only pre-existing
  tinyobjloader warnings).
- **run 1 — north star:** `minimal.exe` launched with working directory `C:\` (deliberately
  wrong cwd). boot → 3 windows (surfaces created **on MAIN** via reconcile) → default shader
  loaded from `vent://shaders/default.slang.spv` → 64² checkerboard placeholder + viking room
  (3566 verts / 11484 idx) + 1024² texture → delayed 1920×1080 window at frame 60 (own
  swapchain, own uniform ring, clean) → 180 frames @ ~58 fps → exit 0. **zero `[E]` lines, zero
  vuid violations**, no fallback-camera warning (the camera entity drove the view).
- **run 2 — stress (temporary test block, reverted after):** second entity with a *different*
  texture (512² `texture.jpg`) → two set-1 binds per frame, three textures alive; second camera
  assigned to auxiliary window 1 via `set_active_camera(cam2, window)` → different view
  matrices written to different windows' uniform rings — the exact scenario of the old g-1
  race. one `create_mesh` for two entities (model cache dedup). **exit 0, zero errors, zero
  vuids.**
- **run 3 — final:** reverted client re-built and re-run, same clean result.
- grep confirms no leftovers of: `update_global_uniforms`, `_global_uniform`,
  `_global_descriptor_sets`, `renderer()->begin_frame`, `renderer()->set_camera`,
  `_window_sub`, `create_global_uniforms`.
- AGENTS.md updated in the same change set (T-3): frame contract, pipelines/textures-immutable
  invariant, mount defaults.

---

<a name="not-done"></a>
## intentionally NOT done (and why)

- **asset loading is still synchronous first-touch** — the first frame that sees a new mesh
  pays disk + parse + a gpu stall. that is phase 2's entire subject (handles, job-based
  loading, staging ring); building it now on phase-1 deadlines would have produced a worse
  version of it.
- **string-keyed caches remain** (`model_path` as map key, hashed per lookup). replaced by
  generation-checked asset handles in phase 2.1 — `mesh_component` will store handles resolved
  once at `set_mesh`.
- **window pointers can still dangle** after a user-initiated close (review C-5). reconcile
  compares without dereferencing, so phase 1 is no worse than before — the real fix
  (generation-checked window handles) shares machinery with asset handles, so it lands with
  them.
- **the sort key is still the entity id.** real key packing is roadmap 3.1; the bind-on-change
  elision that will exploit it is already in place.
- **standalone-sdk cmake gap:** the in-workspace `vent_create_client` ships engine assets; the
  copy inside `vent-config.cmake.in` (for external sdk consumers) does not yet — it needs the
  sdk itself to carry the compiled engine assets. flagged in a comment at the site.
- **`--no_async` / low-worker configs** were not re-audited this phase; the renderer's
  `task.get()` during recording still relies on help-based waiting (review C-7 rules apply
  unchanged).

these are the honest edges: each is a deliberate scope cut with its phase named, not an
oversight.
