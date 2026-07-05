# vent engine — threading & lifetime reference

**date:** 2026-07-05
**purpose:** one page to answer "who is allowed to touch this, from which thread, when?" —
first for the codebase *as it is*, then as it *should be*. keep this updated as the roadmap
lands; it's the document AGENTS.md's threading rules should eventually absorb.

---

## 1. the threads

| thread | created by | lifetime | runs |
|--------|-----------|----------|------|
| `MAIN` | os | process | engine entry, bootstrap init, main loop, client `on_update`, renderer begin/end frame, shutdown |
| `PLAT` | platform_system::initialize | platform init → shutdown | glfwInit/Terminate, event pump (`glfwWaitEventsTimeout`), **all** glfw window calls (marshalled via `invoke_on_platform_thread`), window destruction on close, `window.created/destroyed` publishing |
| `VLOG` | log_system::initialize | log init → shutdown | drains the mpsc log queue, console + file writes, rotation |
| `W:00..NN` | job_system::initialize (one per hw thread — see roadmap perf item on oversubscription) | job init → shutdown | all `job()->fire/submit` work: **event-bus callbacks**, system initialization, command-buffer recording, (phase 2:) asset io |

things that are easy to forget:

- **every event callback runs on a W thread**, never on the publisher's thread. subscribing code
  must be safe against that (the renderer's `window.created` handler creates vulkan surfaces on
  a worker — legal in vulkan, but only because nothing else touches the backend concurrently at
  that moment).
- **system `on_initialization` runs on a W thread** for regular systems (bootstrap systems run
  on MAIN). anything a system touches in init is potentially concurrent with every other
  system's init.
- in debug builds `log()` is the synchronous fallback — VLOG is idle. release behaves
  differently. (review §7.10 suggests unifying this.)

---

## 2. data ownership map — current state

legend: ✅ safe as used today · ⚠️ works by timing/luck (latent) · ❌ racing today (UB every run)

| data | owner | writers | readers | verdict |
|------|-------|---------|---------|---------|
| `system_registry::_systems`, `_interfaces` | MAIN (intended) | MAIN + **W (renderer init → add_system)** | all threads via accessors | ❌ review C-1 |
| `system_entry::state` | registry | W (mark_ready) | W/MAIN (is_ready) | ❌ plain enum, C-1 |
| `system_entry::pending_dependencies` | creator | multiple W (event callbacks) | W | ❌ review C-3 |
| `_init_order` | registry | W | MAIN (shutdown) | ✅ mutexed |
| event bus `_subscriptions` | event_bus | any (subscribe/unsubscribe) | any (dispatch snapshot) | ✅ shared_mutex — but in-flight-callback semantics undefined (C-3.2) |
| job queues (mpmc, deques) | job system | per contract | per contract | ✅ (deque pop nit, review §8) |
| `platform_system::_windows` | **PLAT** | PLAT only | any (getters, mutexed) | ✅ the cleanest module in the engine |
| `window` members | window | PLAT (via marshal) + glfw callbacks (PLAT) | any (mutexed getters) | ✅ |
| raw `ic_window*` held by client/renderer | — | PLAT deletes | MAIN uses | ⚠️ dangles after user-close — review C-5 |
| backend `_surfaces` | backend | MAIN (begin_frame sweep) + W (create via event) | MAIN + W | ✅ shared_mutex + deferred destroy |
| backend `_meshes`/`_textures` | backend | MAIN (create/destroy) | W (record) | ✅ mutexed — but lock held across gpu stalls (review G-4) |
| backend `_pipelines` | backend | MAIN | **W (record, unlocked)** | ⚠️ invariant real but undocumented — review G-5 |
| global ubo ring + descriptor sets | backend | MAIN (per window!) | GPU | ⚠️/❌ cross-window gpu race — review G-1 |
| `_active_swapchain`/`_active_window` | backend | MAIN (begin/end) | W (record) | ✅ only via happens-before of job submit… as long as recording jobs never outlive end_frame — undocumented invariant |
| asset caches | asset system | any | any | ⚠️ TOCTOU + unlocked shader path — review C-4 |
| renderer `_model_cache`/`_texture_cache` | renderer | MAIN (end_frame, unlocked) | MAIN | ✅ today (single-threaded) — the new mutexes must be used consistently or removed (C-4) |
| `world_system` maps | world | MAIN | MAIN | ✅ today; ❌ the day gameplay jobs mutate the world (S-7) — extraction snapshot is the plan |
| `main_loop::_runnables` | main_loop | MAIN (sync_runnables) + **any (set_runnables via cache_role_interfaces)** | MAIN | ⚠️ review C-8 |

---

## 3. the rules (target model — adopt as amendments to AGENTS.md)

these are the seven rules that, once true, make every ⚠️/❌ above disappear. they're written as
laws so future-you can quote them in reviews.

**R1 — phase freeze.** shared structures are *built* single-threaded, *frozen*, then read from
anywhere. the registry after init, pipelines during a frame, the world during extraction —
mutation only at documented phase boundaries. (a structure that is never mutated concurrently
needs no lock.)

**R2 — one owner per mutable datum.** every mutable field names an owning thread in its `///<`
comment. other threads reach it via messages (platform command queue — the house example),
snapshots, or handles. if you can't name the owner, the design isn't finished.

**R3 — callbacks capture owners, not stack.** anything registered with the event bus / job
system captures by value, by shared-lifetime, or by handle — never `&local`. subscriptions are
RAII (`scoped_subscription`), so the compiler proves they die before their captures.

**R4 — no job blocks on a job** unless it (a) actively drains *all* queues while waiting, or
(b) waits on a c++20 `atomic::wait` that a `notify` provably reaches. never a bare spin.
corollary: never call a blocking engine api (`invoke_on_platform_thread`, `publish_wait`,
`task::get`) from a callback whose completion the blocked thread is itself waiting on — that's
the PLAT↔worker deadlock shape (review C-3.5).

**R5 — the fence question.** before any thread writes memory the gpu may read (or destroys a
gpu resource), name the fence/timeline value that proves the gpu is done with it. if the answer
is "waitIdle", the design isn't done (allowed only at final shutdown). per-frame resources are
indexed by the ring slot that fence protects — per window, `MAX_FRAMES_IN_FLIGHT` copies.

**R6 — handles across domain boundaries.** pointers don't cross ownership domains (engine ↔
client, thread ↔ thread, frame n ↔ frame n+1). generation-checked handles do, and a stale
handle is a *checkable error*, not UB. applies to: assets, windows, entities, gpu resources.

**R7 — concurrency ships with its test.** a new lock-free structure, lock protocol, or async
pipeline lands together with a storm test that runs under tsan on the linux preset. no
exceptions — this codebase already contains three hand-rolled lock-free structures and zero
tests for them.

---

## 4. quick reference — "can i call X from thread Y?"

current truth table for the apis you touch most (update alongside the code):

| call | MAIN | PLAT | W:xx | notes |
|------|------|------|------|-------|
| `log()->...` | ✅ | ✅ | ✅ | lock-free queue (release) / sync fallback (debug) |
| `job()->fire/submit` | ✅ | ✅ | ✅ | worker-submitted jobs go to that worker's local deque — external waiters can't help with them (R4/C-7) |
| `task::get / wait` | ✅ | ⚠️ | ⚠️ | spins helping global queues only; on PLAT it can starve the event pump; inside a job see R4 |
| `event()->publish` | ✅ | ✅ | ✅ | async, unordered, callbacks on W |
| `event()->publish_wait` | ✅ | ⚠️ | ⚠️ | blocks until callbacks done; from PLAT, callbacks must not marshal back to PLAT (deadlock) |
| `platform()->create_window / destroy_window` | ✅ | ✅ | ✅ | marshalled + blocking; from W it's fine but holds the worker |
| `window::set_title / resize / show...` | ✅ | ✅ | ✅ | marshalled; **not** from a `window.created/destroyed` callback while a `publish_wait` from PLAT is pending |
| `renderer()->begin/end_frame` | ✅ | ❌ | ❌ | frame apis are MAIN-only by design (and become engine-internal in roadmap 1.1) |
| backend `create_mesh/texture` | ✅ (via end_frame today) | ❌ | ⚠️ | technically mutex-guarded, but stalls the gpu + holds `_mesh_mutex` across the stall |
| `world()->set_* / get_*` | ✅ | ❌ | ❌ | world is MAIN-only until extraction snapshots exist |
| `system().get<T>()` | ✅ | ⚠️ | ⚠️ | safe only while the registry is frozen (post-init, pre-shutdown, no runtime plugin loads) — review C-1 |

---

## 5. mental models worth keeping

- **the platform module is the pattern.** one owning thread, a command queue in, mutexed
  snapshots out, blocking marshal helper with inline fast-path. when in doubt about how to make
  a subsystem threaded, copy `platform_system`, not `system_creator`.
- **a fence is a proof, not a pause.** every vulkan sync primitive answers "what has provably
  happened?" — the bugs (G-1, G-3) happened where code paused (*its own* fence) but needed proof
  about *someone else's* work.
- **the two-list trick** (`_pending_add`/`_pending_remove` + integrate at frame start, as in
  `main_loop`) is the cheapest correct way to let any thread request a change that only the
  owner applies. it's already in the codebase; reuse it before inventing anything fancier.
- **deletion is deferred everywhere in engines.** surfaces (you already do it), gpu buffers
  (roadmap 2.4), subscriptions (drain-on-unsubscribe), entities (end-of-frame kill lists) —
  "mark now, reap at the next proven-safe point" is the same idea four times.
