# why i keep avoiding the event system — and why you're mostly right

> a direct answer to your pushback on `20260707_0134_claude_architectural_questions.md`.
> short version: i wasn't avoiding *events*. i was avoiding *this event bus's single
> delivery mode*, and then i over-generalized that avoidance into a principle it didn't earn.
> you caught a real inconsistency. below is the honest accounting, then a concrete proposal.

---

## 0. the one-paragraph answer

The event bus is not implemented *wrong*. It is implemented *under-powered*: it has exactly
**one** delivery policy — "run the callback on some job worker, in no order, possibly
concurrently" — and that one policy happens to be the single most dangerous way you could
possibly deliver a window-surface event. So when I hit window→surface sync, I didn't reach for
"a better event", because the bus can't express one. I reached for polling. Then, instead of
saying *"the bus needs a second delivery mode"*, I wrote *"prefer polling over events"* into
the handbook as if it were a law of engines. That was the mistake. Your instinct — *"is it
implemented wrong? should it decide which threads to use?"* — points at exactly the missing
feature. It should decide which thread. That's the whole fix.

---

## 1. the contradiction you found (it's real, and it's in my own words)

My Part 1 decision rule was:

- **events win** when changes are **rare and unpredictable**;
- **polling wins** when you're **already looping**, the state is **small**, and there's a
  **safe reconcile point**.

Then two paragraphs later I moved window creation — *"one of the rarest things an engine ever
does"*, as you correctly put it — onto the **polling** side. By my own rule, "rare and
unpredictable" is the *textbook* case for events. I argued the opposite and hoped the "you're
already looping anyway" clause would cover it.

It doesn't, cleanly. Here's the tell: **frequency was never the real reason.** Re-read what
actually drove the change (it's even in the bus header, lines 63–68):

```
// deadlock warning: platform events (window.created / window.destroyed) are
// published with publish_wait() FROM the platform thread. a subscriber to
// those events must not call any window method that marshals back to the
// platform thread ...
```

The real driver was: **the callback runs on the wrong thread, at an unknown time, and can
deadlock against PLAT.** That is a property of *how this bus delivers*, not of events as an
idea, and *not* of how often windows are created. I dressed a **threading** problem up in a
**frequency** argument, and the frequency argument happened to point the other way. You pulled
that thread and the sweater came apart. Good.

**So: checking the window list every frame in the hot path is, on the merits, silly.** It's a
diff of a ~1-element set that changes approximately never, run 60×/second forever. The *only*
reason it exists is that the bus couldn't hand me a callback on the main thread at a safe
moment. Give it that ability and `reconcile_surfaces` should not exist.

---

## 2. so — is the event system implemented wrong? (precise answer)

Not *wrong*. **Missing three things.** Here's the current contract, verbatim from
`ic_event_bus.hpp`, and what each line is really telling you:

| the contract says… | what it really means | the cost |
|---|---|---|
| "callbacks run on job-system worker threads, NOT the publisher's thread" | **delivery thread is fixed and non-negotiable** | you can never say "deliver on MAIN" or "deliver inline" |
| "in any order … may run concurrently" | **no ordering, ever** | can't express "B reacts after A" without a second event |
| callback is `std::function<void(string_view, void*)>` | **stringly-typed name + type-erased `void*`** | no compile-time payload safety, a hash per publish |
| `subscribe` / `publish` / `publish_wait` | **no way to *await* an event from linear code** | you can only pre-register a callback; you can't "run until event E, then continue" |

None of these is a bug. Each is a *design ceiling*. The three that matter for your questions:

1. **No delivery-thread choice.** This is *the* one. One mode: job workers. The most
   concurrency-hostile mode available, applied uniformly, even to events whose subscribers
   fundamentally need a specific thread (anything touching Vulkan, anything touching a window).

2. **No awaitability.** Your vision — *"run all `ir_runnable`s in parallel and wait for fired
   events to continue"* — is a **synchronization** use of events. This bus only does
   **notification**. `publish_wait` blocks the *publisher* until callbacks finish; it gives the
   *subscriber* no way to say "suspend me here until E fires." That gap is exactly why your
   "sync on events" model can't be built on today's bus.

3. **No typing / no ordering.** Lower priority, but a "perfectly designed" bus would carry
   typed payloads and offer *opt-in* ordering where a subscriber asks for it.

**Verdict:** the bus isn't broken, it's a v1 that only implements the hardest-to-use-safely
delivery mode. Every place I "avoided events" is a place where a *second* delivery mode would
have let me use events safely. I should have proposed that mode. I'm proposing it now (§5).

---

## 3. "should it decide which threads to use?" — yes, and you already built the pattern once

You asked two versions of the same question:

- *"should i make [the event system] able to decide which threads to use?"*
- *"we could also modify the job system to optionally force a job to a specific worker…"*

These aren't two ideas. They're **one primitive at two layers**, and the lower one is the right
place to put it:

> **Give the job system thread-affinity** ("run this job on thread X, at X's next safe pump
> point"). Then **event delivery policy is just: which thread do I pin this subscriber's
> callback-job to.**

And here's the thing — **you have already implemented this exact mechanism once**, for PLAT.
`invoke_on_platform_thread` (platform module) is a single-thread-affinity job queue: any thread
can hand PLAT a closure, PLAT runs it at its next pump, with an inline fast-path when you're
already on PLAT. That is *precisely* the shape of the missing job feature, generalized from "the
one PLAT queue" to "any registered thread has an affinity inbox." MAIN especially needs one — a
MAIN inbox drained at frame start is the delivery target that makes window events safe *and*
deletes `reconcile_surfaces`.

So the answer to "decide which threads" is: **yes — build thread-affinity in the job system
(generalizing the PLAT marshaller), and let the event bus name a delivery thread per
subscription.** One primitive, and it pays off twice (jobs *and* events). Note the job API
already carries a `job_priority` enum but no affinity — affinity is the natural sibling of
priority, and both belong on the same submit path.

---

## 4. the big question: given a *perfect* event system, would i keep vent's decisions?

I went through every phase-1 decision and sorted them by whether a great event system changes
them. This is the most useful part of the doc, because it shows how *little* of vent is actually
about events — and that the parts that aren't are the parts that **enable** your parallel
vision.

| decision | about events? | verdict under a perfect event system |
|---|---|---|
| window→surface sync (polling) | **yes** | **flips back to events.** delivery-on-MAIN-at-frame-start subscription; `reconcile_surfaces` is deleted. you were right. |
| sequential phase-ordered frame | **yes** | **flips to parallel-by-default**, with events as the sync primitive between the few real dependencies. phases survive as the *coarse* layer only (see below). |
| extract/snapshot (`extract_frame`) | no | **kept, unconditionally — and it's what makes your parallelism *safe*.** a snapshot turns a data race into a memcpy. events don't replace it; they need it. |
| engine owns the loop (IoC) | no | kept. orthogonal. |
| retained-mode world | no | kept. orthogonal. |
| per-window uniform ring / fence scoping | no | kept. this is GPU correctness, not events. |
| handles across boundaries | no | kept — and it *enables* parallelism (safe cross-thread identity). |

**Read the "about events?" column.** Out of seven load-bearing decisions, **two** are actually
about events. The other five are orthogonal, and three of them (snapshot, fences, handles) are
the *machinery your async-only principle needs to be safe*. So the honest picture is not "vent
avoids events everywhere." It's "vent made one genuinely event-shaped decision (window sync) and
I got it backwards, plus one default (sequential frame) that should eventually flip."

**What would I *transfer to* events, given a good bus?** Two things, exactly the two "yes" rows:

1. **window lifecycle** → `window.created`/`window.destroyed` with a **MAIN-delivery** policy.
   Event-driven, safe, off the hot path.
2. **inter-system frame synchronization** → the `ir_runnable` sync points. Systems run free;
   where B genuinely needs A's output, B *awaits* an event A fires. This is your model, and it's
   the right one — *once the bus can await and the data is snapshot-safe.*

---

## 5. the model you actually want (bevy-flavored) — and the one honest catch

You wrote the design yourself, and it's coherent and known-good:

> everything that can be async must be async; **sync only when we explicitly want to** — e.g.
> run all `ir_runnable`s in parallel, and *wait for fired events to continue*.

That is Bevy's stance ("parallel and unordered by default; order only where forced"), expressed
through events instead of through `.before()/.after()`. It is a real architecture that real
engines ship. I'm not going to talk you out of it. But there is **one** engineering constraint I
have to put on the table plainly, because it's the crux and it's non-negotiable physics:

> **"Parallel by default" is "races by default" unless *something* proves two systems that run
> at the same time don't touch the same data unsafely.** Bevy gets that proof from **declared
> data access** (every system states its `Query<&A, &mut B>`; the scheduler builds a conflict
> graph and only parallelizes non-conflicting systems). Take the proof away and unordered
> parallelism silently corrupts state.**

So your model *requires* one of these three, and you should pick deliberately:

- **(a) declared data access** (the Bevy route). Systems declare what they read/write; the
  scheduler auto-parallelizes and auto-serializes. Most powerful, most machinery. It's a real
  ECS project on its own.
- **(b) everything the systems share is a concurrent/immutable structure** — so simultaneous
  access is *defined* regardless of order. This is your "make the data structures parallel"
  paragraph.
- **(c) snapshot + handoff** — systems don't share live mutable state at all; each frame you
  freeze inputs (extract) and hand owned copies across the seam. Order stops mattering for
  *safety* (only for *logic*, where events sync it).

Here's the part worth internalizing, because it reframes your "make the data structures
parallel" instinct: **(c) is not a retreat from async — (c) is often the *best* form of async.**
A concurrent data structure (route b) gives you *contended* access — atomics, cache-line
ping-pong, and frequently *nondeterminism*. A snapshot (route c) gives you **lock-free,
uncontended, deterministic** reads by every worker simultaneously, because they're reading
frozen private memory. That's why `extract_frame` exists, and it's why every AAA renderer copies
the scene rather than sharing it. So: don't read my snapshot advocacy as "single-threaded
timidity." Read it as **"the cheapest way to let 16 cores read the same data at once is to give
each of them a copy nothing can write."** Your async-only principle is *better served* by
snapshots than by making the world itself thread-safe.

The mature synthesis — and I think it's genuinely what you want — is **all three, layered**:

- **snapshot/handoff (c)** for the big per-frame data (render input) — already done.
- **concurrent structures (b)** only where handoff genuinely doesn't fit (the job queues
  themselves, a future event ring).
- **declared access (a)** as the *eventual* scheduler smarts, if/when you want true
  auto-parallel systems — but you don't need it to *start*, because…
- **events (awaitable, thread-targeted)** are the explicit sync primitive for the *few* real
  cross-system dependencies, which is exactly your "sync only when we want to."

And **fibers** (which you flagged) are the natural *implementation* of "await an event without
burning a thread": a system that awaits E yields its fiber; the scheduler runs other fibers;
when E fires the fiber resumes (possibly on another thread). That is the clean endpoint of your
model. It's also the biggest single build. Sequence it **last**, after the primitives below,
because you can get 80% of the vision without it.

---

## 6. what "phases" become in this world (so the phase work isn't wasted)

Your Bevy instinct might read as "so the phase list was a wrong turn." It wasn't — it becomes
the **coarse layer**, and even Bevy keeps it (schedules/stages: `Update`, `FixedUpdate`). The
end state is two layers, and vent already has the first:

- **between phases: ordered.** input → simulate → extract → render. coarse, sequential,
  deterministic. This is what `run_phase()` already gives you. Keep it.
- **within a phase: parallel + unordered**, safety guaranteed by (a)/(b)/(c), logic ordered by
  events where needed. This is the part that doesn't exist yet.

So `run_phase` stays as the escape-hatch/coarse frame skeleton; the default *within* a phase
flips from "sequential stable-sort" to "fan out, sync on events." Nothing built gets thrown
away — the sequential default was always the honest *starting* point for an engine that couldn't
yet prove non-conflict, exactly as noted in the original doc's Bevy paragraph.

---

## 7. concrete proposal (small, ordered, testable — not a rewrite)

Per AGENTS.md I won't start a large refactor without your say-so, and per "no workarounds" this
is designed as real architecture, not a patch. Ordered so each step is independently valuable
and low-risk. **Step 0 is the one you specifically called me out on and it has no excuse.**

**Step 0 — storm-test the lock-free containers *first*.** You asked *"why don't we just
implement tests for the queues?"* — there is no good answer. The handbook calls untested
lock-free code "the single scariest fact in the codebase," yet I keep *citing* that instead of
*fixing* it. Concretely: a doctest/catch2 target on the linux preset (for tsan), plus storm
tests for `mpmc_queue`, `work_stealing_deque`, `mpsc_queue` — N producers / M consumers,
assert count-in == count-out, no duplicates, no drops, run under ThreadSanitizer. This must come
first because **every step below leans harder on these queues**, and "parallel by default" on
untested lock-free primitives is how you get bugs that reproduce once a month. I can start here
immediately if you want — it's self-contained and needs no architectural decision from you.

**Step 1 — job thread-affinity.** Generalize `invoke_on_platform_thread` into the job system:
`fire`/`submit` gain an optional target thread (affinity as a sibling of the existing
`job_priority`). Register MAIN as an affinity target with a frame-start drain. This is the
enabling primitive for everything after it, and it's mechanically close to code you've already
written and trust.

**Step 2 — event delivery policies.** Add a per-subscription delivery policy to `subscribe`:
`inline` (publisher's thread) · `on_thread(id)` (built on step 1) · `parallel` (today's
behavior, still the default) · `at_sync_point` (queued, drained at a named barrier like frame
start). The moment `on_thread(MAIN)` exists, window events become safe and
**`reconcile_surfaces` is deleted** — the deadlock warning in the header (lines 63–68) can be
deleted with it, because the callback no longer lands on a worker that fights PLAT.

**Step 3 — awaitable events.** Add `co_await`-able / fiber-suspendable event waits so a system
can *run until* an event, then continue. This is what unlocks your literal "run in parallel,
wait for fired events to continue." Best done once step 1's affinity + a fiber or coroutine
runtime is in place — this is the biggest step and the right place to pause and design together.

**Step 4 — (optional, later) typed events + opt-in ordering.** Kills the stringly-typed `void*`
and the per-publish hash; lets a subscriber ask for ordered delivery where it actually needs it.

Steps 0–2 are small and unlock the concrete win you pointed at (window events back on events,
off the hot path). Step 3 is the real "vent's soul" work and deserves its own planning pass.

---

## 8. the actual answer to "why do you desperately avoid it"

Because the bus offered me one delivery mode — the dangerous one — and instead of asking you for
a second mode, I wrote the avoidance into doctrine. That was me optimizing around a v1 ceiling
and mislabeling it a principle. The event system isn't bad. It's *young*, and it's missing the
one feature (**delivery-thread choice**, then **awaitability**) that would make it the backbone
of exactly the async-first, sync-only-when-we-mean-it engine you're describing — the one where
`extract`/fences/handles aren't in tension with events but are the safe ground events stand on.

You didn't design a bad event system. You designed a v1 and I treated its ceiling as a verdict.
Let's raise the ceiling. If you're good with it, I'll start on **Step 0 (the queue storm
tests)** right now, since it's the thing you explicitly asked for and it blocks nothing.
