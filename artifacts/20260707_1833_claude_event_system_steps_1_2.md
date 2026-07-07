# event-system rework — steps 1 & 2 (job affinity + event delivery policies)

**date:** 2026-07-07 · **scope:** job system, event bus, main loop, public sdk.
**companion:** `20260707_1727_claude_event_system_rethink.md` (the plan) ·
`20260707_1811_claude_container_tests_and_deque_fix.md` (step 0).

Implements steps 1 and 2 of the event-system plan, plus one honest correction to the plan
(step 2b) that reading the actual renderer forced. Step 3 (awaitable events) is designed
below but **not built** — it's a runtime subsystem and wants a checkpoint.

---

## step 1 — job thread-affinity

**what.** A `job_affinity { any, main }` enum, the sibling of `job_priority`: priority says
*when* a job runs, affinity says *where*. `fire`/`submit` take an optional `affinity` (default
`any` — nothing existing changes). Jobs pinned to `main` route into a dedicated
`_main_affinity_queue` that **only the main thread drains**, via a new `run_pinned_jobs()`
the main loop calls once per frame at frame start. Workers never touch that queue, so pinned
work is never stolen off the main thread and runs at a known, deterministic point.

**why.** This generalizes the platform module's `invoke_on_platform_thread` marshaller (a
single-thread inbox for PLAT) to the main thread — the missing primitive the whole design
rested on. It's the substrate for `event_delivery::main` (step 2) and, later, for safely
mutating main-owned state (the world) from a worker-originated request.

**key correctness points (all commented in-code):**
- `_main_thread_id` is captured in `initialize()`, which runs on the main thread (job is a
  bootstrap system) — so it's exactly the thread that later drains the inbox.
- `submit_job` checks affinity *first*, before worker-local/global routing, so a pinned job
  can never be enqueued to the wrong place.
- inbox-full fallback: if on the main thread, run inline (safe, avoids self-deadlock while
  main is the sole drainer); otherwise spin until main frees space (never run a `main` job on
  the wrong thread — that would defeat the point).
- `drain()` also calls `run_pinned_jobs()` (a no-op off-main) so shutdown, which drains on
  main, can't spin forever on outstanding pinned work.

**the deadlock rule it introduces:** never block-wait on a `main`-pinned job from a non-main
thread — only the main-loop drain runs it, so the waiter would spin forever.

**files:** `_vent/job/ic_job.hpp`, `modules/job/public/job/interfaces/i_job.hpp`,
`modules/job/private/job_system.hpp`, `modules/job/src/job_system.cpp`,
`modules/core/private/fallback/fallback_job.hpp` (fallback matches the new signatures;
`run_pinned_jobs` is a no-op there — everything already runs inline),
`modules/core/src/main_loop.cpp` (the per-frame drain).

**validated:** full engine builds clean; `minimal` runs 180 frames @ 58.7 fps calling
`run_pinned_jobs()` every frame, clean shutdown (exit 0). No frame-rate impact.

---

## step 2 — event delivery policies

**what.** `subscribe(event, callback, delivery)` where `event_delivery` is:
- `parallel` (default) — on a job worker, unordered, possibly concurrent. today's behavior.
- `main` — deferred onto the main thread's frame-start drain (built on step 1's affinity).
  serialized, safe for callbacks that touch main-owned state.
- `immediate` — synchronously on the publisher's thread, before `publish` returns.

`dispatch` routes each subscriber by its own policy; the same event can wake different
subscribers on different threads. This is the concrete answer to the original question
("should the event system decide which thread to use?") — yes, per subscription.

**the `publish_wait` × `main` rule (a real deadlock, handled).** `publish_wait` blocks for
`parallel`/`immediate` subscribers but **does not block on `main` subscribers**. Awaiting a
main-deferred callback from another thread re-creates the exact platform/worker deadlock the
whole exercise is about: e.g. main calls `create_window` → marshals to PLAT and blocks; PLAT
publishes `window.created` and waits on a main-delivered callback; main (blocked in
`create_window`) never drains it → both hang. So a `main` subscriber always runs at the next
frame boundary regardless of publish vs. publish_wait. Documented in the `ic_event_bus.hpp`
contract; the one caveat is that `publish_wait` callers must keep `data` alive past the frame
for `main` subscribers (same shape as the existing fire-and-forget data warning).

**files:** `_vent/event_bus/ic_event_bus.hpp` (enum, `subscribe` param, contract update),
`modules/event_bus/private/event_bus_system.hpp`, `modules/event_bus/src/event_bus_system.cpp`.

**validated:** the engine's parallel init is entirely event-driven (`system.ready.*` →
dependency resolution); it still boots and resolves correctly, confirming the default
`parallel` path is untouched. 180 frames, clean shutdown.

---

## step 2b — the correction: `reconcile_surfaces` stays

The plan said "the moment `on_thread(main)` exists, window surface creation goes back to
events and `reconcile_surfaces` is deleted." **Reading the actual reconcile code, I'm
reversing that.** `reconcile_surfaces` (renderer `on_update`, frame start) is ~15 lines that:

- create a surface for any platform window that lacks one (covers startup *and* runtime
  windows, idempotently), and
- destroy any surface whose window vanished from the platform list — comparing the window
  pointer only, **never dereferencing it**, which is exactly what makes the destroy path
  safe against a window the platform already freed.

An event-driven version would be *worse* here: the create path could move to a `main`-delivered
`window.created` subscription, but the **destroy** path can't safely ride a deferred event —
a `window.destroyed` payload is a raw `ic_window*` the platform frees, so handling it a frame
later is a use-after-free until window identity is stable (generation-checked window handles,
which the roadmap has but hasn't built). So events would split one robust, idempotent
convergence loop into two mechanisms, one of which needs machinery that doesn't exist yet.

**Conclusion:** reconcile is the right pattern for surface lifecycle — it's the "converge to a
source of truth at a known safe point" idea, which is *superior* to events for this job. The
delivery-policy primitive's real first consumers are **world-mutating event handlers** (a
gameplay system reacting to an event by spawning/mutating entities — the world is main-only,
so `main` delivery is exactly right) and **step 3**. When window handles land, the surface
create-path *may* move to events; the destroy-path convergence likely stays. This is the same
lesson as the original rethink: events aren't universally better; they're better for
sparse/awaitable signals, and convergence-polling is better for derived per-frame state.

---

## step 3 — awaitable events (designed, NOT built — checkpoint)

The remaining piece is "run systems in parallel, sync on fired events" — events as a
*synchronization* primitive a system can **await**, not just a notification it pre-registers a
callback for. Today's bus can't do this: `subscribe` registers a callback; there's no "suspend
here until event E fires, then continue." Two ways to build it:

1. **C++20 coroutines.** An `co_await event("foo")` returns an awaitable that registers a
   one-shot continuation and suspends the coroutine; `publish` resumes it (on a chosen
   delivery thread, via step 1/2). No new thread machinery — the coroutine frame holds the
   suspended state on the heap. Cleanest for "a task awaits an event"; integrates with the job
   system if jobs can be coroutines. Cost: the job/task types need a coroutine-aware path;
   lifetime of the coroutine frame across the await needs care.
2. **Fibers** (Naughty Dog / your own note). Each system runs on a fiber; awaiting an event
   yields the fiber, the scheduler runs others, `publish` reschedules it (possibly on another
   thread). The most powerful ("everything async, sync only on events" falls out naturally)
   and the most work — it's a scheduler + a context-switch layer + stack management, a real
   subsystem.

**Recommendation:** start with **(1) coroutines** for `co_await event(...)` — it delivers the
awaitable-event experience with far less surface than a fiber runtime, and it composes with
the job system we already have. Treat fibers as a later migration if/when the coroutine model
hits a wall (deep call stacks that want to suspend without being coroutines all the way down).

**Why I stopped here rather than build it:** per AGENTS ("propose before large features",
"never rewrite large parts unless requested") a coroutine/fiber runtime is a genuine subsystem
with its own design decisions (how a job becomes awaitable, frame/stack lifetime, which thread
resumes) — worth deciding together before code, and it wants its own test rig. Steps 1 & 2
are the foundation it builds on and are done, validated, and independently useful today.

**Decision for you:** (a) go coroutines now, (b) go fibers now, or (c) hold step 3 until a
concrete consumer needs awaitable events (there isn't one in-tree yet — same "land it with its
first user" discipline that applies to the delivery policies).

---

## state after this session

- steps 1 & 2 built, full engine green, `minimal` runs clean (180 frames, exit 0).
- handbook updated (§7 boot loop, §8 frame, §9.3 event_bus, §9.4 job, §12 map) to the current
  design — no bug/timeline narrative; this artifact carries the reasoning.
- nothing committed.
- open: the `reconcile` decision above (surfaces stay convergence-based); step 3 direction.
