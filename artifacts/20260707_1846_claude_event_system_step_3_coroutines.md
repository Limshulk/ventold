# event-system rework — step 3 (awaitable events via coroutines)

**date:** 2026-07-07 · **scope:** public sdk (`co_task`, `await_event`), minimal testbed,
test rig. **companion:** `20260707_1833_claude_event_system_steps_1_2.md` (affinity + delivery
policies, which this builds on).

Implements the third and last piece of the event-system plan: events as a **synchronization**
primitive a coroutine can `co_await`, not just a callback it pre-registers. Chosen approach:
C++20 coroutines (over fibers) — the awaitable-event experience with a fraction of a fiber
runtime's surface, composing with the job system already in place.

---

## what

Two header-only additions to the sdk:

- **`_vent/core/co_task.hpp`** — `co_task`, a detached, fire-and-forget coroutine. Starts
  eagerly, suspends at its first `co_await`, resumes when that awaitable completes, and frees
  its own frame on return. The simplest possible coroutine type: no result, no handle, no
  cancellation. Deliberately depends on nothing but `<coroutine>`/`<cstdio>` (its
  `unhandled_exception` uses `fprintf`, not `log()`) so the coroutine machinery is
  **unit-testable without the engine**.
- **`_vent/event_bus/event_coroutine.hpp`** — `co_await await_event("name")` suspends the
  coroutine until the event fires, then resumes it (default: on the main thread). Built
  entirely on step 1/2: the awaiter registers a **one-shot** subscription (self-unsubscribing
  in its callback) whose delivery policy is the resume thread. Returns the event payload from
  `await_resume()`.

```cpp
auto react() -> vent::co_task {
    kick_off_parallel_work();
    co_await vent::await_event("physics.done");  // suspends; no thread blocked.
    spawn_debris();                              // resumes on main when fired.
}
```

## why coroutines, not fibers

Both give "run, then sync on a fired event." Fibers are the more powerful model (a system can
suspend anywhere, mid-callstack) but are a genuine subsystem — scheduler + context switch +
stack pool. Coroutines get the awaitable-event experience with the compiler doing the
suspend/resume, the frame living on the heap, and zero new threading machinery; they compose
with the existing job system (the resume is just a job routed by delivery policy). The call
was made explicitly (see companion doc's step-3 options); fibers remain a future migration if
deep non-coroutine callstacks ever need to suspend.

## how it hangs together (the whole stack, bottom to top)

1. **step 1** — `job_affinity::main` + a main inbox drained by `run_pinned_jobs()` at frame
   start.
2. **step 2** — `event_delivery::main` routes a subscriber's callback as a main-pinned job.
3. **step 3** — `await_event` subscribes with `main` delivery; its callback resumes the
   coroutine. So a coroutine that suspended *anywhere* resumes deterministically on the main
   thread at a frame boundary.

The testbed proves the full chain at runtime: the demo coroutine runs during init **on a
worker (`W:01`)**, suspends on `co_await`, and when `minimal` publishes `demo.milestone` at
frame 60, the coroutine **resumes on `MAIN`** — the delivery policy moved the resumption across
threads, through the affinity inbox, to the frame drain. The one-shot subscription removes
itself on fire.

## validation

- **co_task machinery** — unit tests in the rig (`test_co_task.cpp`, no engine): runs eagerly,
  suspends at `co_await`, resumes to completion; a no-suspension coroutine runs straight
  through. 159/159 total checks green.
- **full `await_event` path** — the minimal testbed demo, at runtime: suspend on `W:01` →
  publish at frame 60 → resume on `MAIN`, one-shot unsubscribe, 180 frames, clean exit (0).
- full engine builds clean, no warnings from the new code.

## honest v1 edges (documented in-code)

- **resume-id store race, non-main resume only.** The awaiter stores its subscription id
  immediately after `subscribe` returns; the callback reads that id to self-unsubscribe. With
  the default `main` resume, the callback can't run before the next frame, so the store always
  wins — race-free. For `parallel`/`immediate` resume a concurrent publish could in theory fire
  between subscribe and the store; noted as a v1 edge (main is the default and is safe).
- **`co_task` is detached** — no result, no cancellation, no join. That's the right primitive
  for "await a signal and continue"; a richer awaitable-with-result coroutine (retrofitting the
  job `task` with a `promise_type`) is a later step if a real consumer needs it.
- **payload lifetime under `publish_wait` + main** carries the same caveat as step 2: a
  main-resumed awaiter runs at the frame boundary, so a `publish_wait` caller must keep `data`
  alive past the frame (unchanged from the step-2 contract).

## state after this session (steps 0–3 complete)

- **step 0**: container storm tests + chase-lev deque fix.
- **step 1**: job thread-affinity + `run_pinned_jobs`.
- **step 2**: event delivery policies (`parallel`/`main`/`immediate`).
- **step 3**: `co_task` + `co_await await_event(...)`.
- all built, full engine green, `minimal` runs clean (180 frames), tests 159/159.
- handbook updated (§8, §9.3, §9.4, §12) to the current design; artifacts carry the reasoning.
- **reconcile_surfaces stays** (convergence beats events for per-frame derived state — see the
  step 1/2 artifact); the delivery/await primitives exist for world-mutating and awaitable
  consumers.
- nothing committed.

**natural next steps (all want a real consumer first):** an awaitable-with-result coroutine if
gameplay needs `co_await`-ed values; window generation-handles (unlock the surface create-path
migration to events, if ever wanted); wiring `vent_tests` into ci with the tsan build.
