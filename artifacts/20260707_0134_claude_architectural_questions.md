> Please explain to me why different decisions have been made. Why you step away from my event-based system, why you did a priority-based ir_runnable list, explore how other engines handle different things... I don't just want to see vent working, I want to understand why it works the way it does and why we chose it to work this way and not some other way. -> what even are other ways vent could have worked...

This is the best kind of question to ask about an engine, so let me go deep. First I need to correct one thing in the framing, because it changes the whole answer.

**What actually changed (because "my event-based system" is two different things)**

There are *two* completely separate uses of events in vent, and I only touched one of them:

1. The **system-initialization** dependency machinery - systems announce `system.ready.<name>`, and dependent systems wait on those events during parallel boot. **This I did not touch**. You asked me to keep it event-based, and I did. It's still the intricate subscribe/fire logic in `system_creator.cpp`.
2. The **renderer's window subscriptions** - the renderer used to `event()->subscribe("window.created", ...)` and `"window.destroyed"` to create/destroy GPU surfaces. **This is what I replaced with per-frame polling (`reconcile_surfaces`).**

So I didn't step away from *your* init system. I replaced a small, different, and - I'll argue - misplaced use of events with polling. But your instinct to ask "why?" is exactly right, because the *reason* those two uses got opposite treatments is the whole lesson. Let me build it up properly.

---
**Part 1 - Events vs. polling is not a "which is better" question**

Beginners (and a lot of intermediate engine code) treat "event-driven" as strictly more sophisticated than "polling every frame." It isn't. They're two tools with a decision rule, and the rule is about **frequency and timing**, not elegance.

**The axis:** how often does the state change, relative to how often you need to react to it?

- **Events win** when changes are **rare and unpredictable**, and reacting late is unacceptable or wasteful. You don't want to check "did a system finish initializing?" in a busy loop a million times a second - you want to be *told* once, when it happens. Initialization is a one-time, sparse, "wake me when it's done" problem. That is the textbook shape for events, which is exactly why keeping your init system event-based is correct.
- **Polling wins** when you're **already looping anyway**, the state set is **small**, and there's a **natural safe moment** to reconcile. Window creation/destruction fits this perfectly: the engine already runs a loop 60 times a second, there are a handful of windows, and the top of the frame is a moment where touching GPU surfaces is provably safe.

Now here's the part that isn't in most tutorials - the renderer's window subscriptions weren't just *unnecessary*, they were **actively dangerous**, and for a reason that generalizes:

Every event-bus callback in vent runs **on a job worker thread** (that's the documented contract). So `create_surface` was running on some random worker, at an unpredictable moment, concurrently with whatever else was in flight. Creating a Vulkan surface there happened to work only because nothing else touched the backend at that instant - luck, not design. And there was a latent deadlock: a `window.created` callback runs on a worker; if any subscriber ever called a window method that marshals back to the platform thread (which is *blocked* publishing the event), you get worker-waits-for-PLAT-waits-for-worker. Frozen engine, from one innocent `set_title()`.

Polling dissolves all of that. `reconcile_surfaces` runs on the **main thread, at a known point** (frame start), **deterministically**. The dangerous "callback on a mystery thread at a mystery time" is replaced with "converge to the truth, here, now."

The general rule I'd write on the wall: *use events for sparse, latency-sensitive, one-shot signals; use polling to keep derived state in sync with a source of truth you already visit every frame. And be very suspicious of any event whose callback runs on a thread the subscriber didn't choose.*

**How other engines handle this exact tension**

This is not a vent quirk - the whole industry has converged on "poll/reconcile for per-frame state sync":

- **Bevy** (the modern Rust ECS engine) is the cleanest example. Instead of firing an event when a component changes, it offers **change detection**: a system can query `Changed<Transform>` and the ECS tracks a dirty tick per component. You *poll* "what changed since I last ran" at your own cadence, on your own thread, in parallel. Bevy deliberately steers you away from events for state that many systems care about, precisely because events create ordering and threading headaches at scale. This is the same reasoning as vent's reconcile.
- **Unreal** uses **delegates** (its event system) for genuinely sparse gameplay signals - `OnActorBeginOverlap`, `OnDestroyed`. But for per-frame derived state like the render scene, it does **not** fire an event per changed object. It *extracts* a copy of the scene each frame (more on this in Part 3). Rare gameplay moment -> delegate. Every-frame state -> extract/poll.
- **Dirty-flag + sweep** is the same idea wearing a third hat: mark on change, reconcile at a safe point. Vent already uses this for deferred surface destruction (`marked_for_destruction`, swept in `begin_frame`). Reconcile is just the "creation" half of the same pattern.

So the decision wasn't "events bad." It was: *this particular state* (which windows have surfaces) is small, per-frame, and has a safe reconcile point - so it belongs on the poll side of the line, and moving it there deletes a deadlock class for free.

---
**Part 2 - Why a priority-ordered runnable list (and why so *simple* a one)**

Start with the problem, because the "why" only makes sense against it. Before phase 1, `main_loop` iterated its runnables in whatever order `cache_role_interfaces()` produced - and that function builds the list by iterating an `unordered_map`. Hash-map iteration order is an implementation accident. With only the client as a runnable, nobody noticed. The moment the renderer became a second runnable, order became load-bearing: the client *writes* transforms, the renderer *reads* them. Render-before-update doesn't crash - it renders one frame stale, and *nondeterministically*, because it depends on hash seeds and standard-library versions. That's the worst class of bug: invisible, unreproducible, "only on my machine."

So ordering had to become **defined**. The question was *how*. There are four real options:

**Option A - Hardcode it in the loop.** Just write `client->update(); renderer->update();` in `main_loop::run()`. Dead simple. Rejected because it destroys the entire point of the runnable role: the loop is supposed to not know or care which systems exist. Hardcoding the order re-couples the loop to specific systems, and every new tickable system (input, physics, audio) means editing the loop. It also can't handle runtime-added runnables (hot-reloaded plugins).

**Option B - A dependency graph between runnables** ("renderer runs after client"). This is what the *init* system does. Powerful, but it's the heaviest hammer in the toolbox: you need cycle detection, topological sorting, and a way to name relationships between systems that may not know about each other. For per-frame ordering, this is wildly over-engineered - and you told me yourself how painful that dependency machinery was to get right. Building a *second* one for the frame loop would be repeating the hardest part of the codebase for a problem that doesn't need it.

**Option C - Event chains** ("client fires 'update.done', renderer listens"). This re-introduces exactly the threading and ordering hazards from Part 1, and turns a straight line into a web. No.

**Option D - A single integer phase, stable-sorted.** Each runnable declares `run_phase()` returning an `i32`; the loop stable-sorts by it. This is what I chose.

Why D specifically, and why the *details* matter:

- **A plain integer is enough** because frame ordering is genuinely one-dimensional. Things happen in a sequence: input -> gameplay -> physics -> extraction -> render -> present. You don't need a graph to express a line. KISS says: use the simplest structure that captures the real shape of the problem, and the real shape here is "a number line."
- **The gaps** (`simulate = 0`, `render = 1000`) let you insert `physics = 500` later without renumbering anyone. This is an old trick from BASIC line numbers and IP-address-style spacing - leave room so insertion is local.
- **`stable_sort`, not `sort`, is the subtle one.** Stable sort preserves registration order among runnables in the *same* phase. That means the frame is byte-for-byte deterministic across runs. If I'd used `std::sort`, two systems in the same phase could swap order run-to-run, and I'd have re-introduced the exact nondeterminism I was trying to kill - just one level down. Determinism is a *debugging feature*: a bug that reproduces is a bug you can fix.

**How other engines do frame ordering - and why they mostly land near D**

Vent's phase list is a small version of what every big engine has:

- **Unreal - Tick Groups.** Every tickable registers into a group: `TG_PrePhysics`, `TG_DuringPhysics`, `TG_PostPhysics`, `TG_PostUpdateWork`, etc. That's *literally* an ordered set of named phases - the same concept as `run_phase`, just with more phases and physics interleaved. Unreal *also* supports fine-grained tick prerequisites (`AddTickPrerequisiteActor`) for the rare case where two specific actors need ordering *within* a group - that's option B layered on top of D, added only where D isn't enough. The lesson: start with phases; add dependencies surgically later *if* you hit a case phases can't express.
- **Unity - two layers.** The `PlayerLoop` has fixed phases (`FixedUpdate` -> `Update` -> `LateUpdate`) - coarse ordering, exactly like vent's phases. Within a phase, **Script Execution Order** lets you assign per-script priorities (an integer!) in the editor. And Unity's DOTS/ECS uses `SystemGroups` with `[UpdateBefore]`/`[UpdateAfter]` attributes - option B again, for the cases that need it. Same pattern: cheap ordered phases first, explicit dependencies as an escape hatch.
- **Godot** - `_process` vs `_physics_process` (two phases), plus a per-node `process_priority` integer. Phases + integer priority. Same shape as vent.
- **Bevy** - `Update` and `FixedUpdate` schedules (phases), `System Sets` for grouping, and `.before()`/`.after()` constraints (dependencies) that Bevy topo-sorts. But - and this is the interesting bit - Bevy's *default* is that systems within a schedule run **in parallel, in unspecified order**, and it uses the component-access graph to auto-parallelize. It only serializes where you declare an ordering. This is the philosophical opposite end from vent, and worth understanding: Bevy says "parallel and unordered by default, order only where forced"; vent (today) says "ordered and sequential, phases make it explicit." Bevy can do this because its ECS *knows* every system's data reads/writes and can prove two systems don't conflict. Vent doesn't have that machinery, so it takes the simpler, sequential, deterministic road - which is the right call for a learning engine at this scale.

The through-line across all of them: **coarse ordered phases are the universal base layer; fine-grained dependency ordering is an escape hatch added only where phases are insufficient.** Vent has exactly the base layer, which is exactly right for where it is. When you later need "physics must run after input but before animation," you'll add phases. When you need "this *specific* system must run after that *specific* one," that's the day to add a dependency escape hatch - and not one day sooner.

---
**Part 3 - The big question: what other ways could vent have worked?**

This is the one I want to spend the most time on, because "why this and not something else" is only answerable if you can see the something-elses. Here are the major forks in the road, the paths not taken, and who walks them.

**Fork 1: Who owns the loop? (Library vs. Engine - the deepest fork)**

This is *the* architectural decision, and phase 1 was entirely about it.

- **Path A - Library / immediate control.** The client owns `main()`, and calls into the engine: `while (running) { renderer.begin(); renderer.draw(mesh); renderer.end(); }`. This is how **SDL, raw OpenGL/Vulkan, raylib, and Dear ImGui** work. The client is in charge; the engine is a toolbox. Vent was *here* before phase 1 - the client drove `begin_frame`/`end_frame`.
- **Path B - Framework / inversion of control.** The engine owns the loop and calls *you* at defined moments (`on_update`). This is **Unreal, Unity, Godot, Bevy**. The name for this is the **Hollywood Principle: "don't call us, we'll call you."** Vent moved *here* in phase 1.

Why B for vent? Because the north star is a creative user with zero graphics knowledge. Path A requires the user to *be* the render programmer - to know the begin/end pairing, to own the camera math, to iterate windows. Every one of those is a chance to get it wrong. Path B lets the user describe *what* exists and never *how* to draw it. The trade-off is real: Path A gives the client maximum control and is simpler to *implement* (the engine is just a pile of functions); Path B is more work to build and slightly less flexible for the client, but it's the only path where "spawn a model and it just appears, correctly paced" is possible. For a general-purpose engine, B is nearly always right. For a graphics *library*, A is right. Vent chose to be an engine, so it chose B - and phase 1 was the payment for that choice.

**Fork 2: Immediate-mode vs. retained-mode scene description**

Closely related but distinct. When the client wants something on screen, does it:

- **Immediate mode** - re-issue the draw every frame: "draw this mesh here, this frame." Dear ImGui is the canonical example. Simple, stateless, but the engine can't optimize across frames because it forgets everything each frame.
- **Retained mode** - register the object once; the engine remembers it and draws it until you remove it. Scene graphs, Unity's GameObjects, Unreal's Actors. Vent's world (entities + components) is retained mode.

Vent chose retained because it's the only mode where the engine can own the loop (Fork 1) - you can can't invert control if the client has to re-tell you the scene every frame. Retained also enables the extract/snapshot pattern below, culling, sorting persistence, and eventually streaming. The cost is state management (you must remember to remove things), which is why entities have explicit lifetimes. This is the "camera as a component, not a `set_camera` call" decision generalized: *describe state that persists, don't issue commands that evaporate.*

**Fork 3: How does render data get from the world to the GPU?**

Once you have a retained world and the engine owns the loop, *how* does the renderer read the scene?

- **Path A - Read the world live during rendering.** The renderer walks the world's components while recording GPU commands. Simplest. This is what vent did before phase 1's extraction step. The fatal flaw: the instant any gameplay code runs on another thread and mutates the world while the renderer reads it, you have a data race with no fix short of locking the entire world during rendering (which kills parallelism).
- **Path B - Extract a snapshot, render from the copy.** Once per frame, copy exactly what the renderer needs into a private structure; render from that. The world is then free to change. This is `extract` -> `prepare` -> `submit`, and it is how every AAA renderer works: Unreal literally copies scene state into `FPrimitiveSceneProxy` objects on the game thread and hands them to the render thread; Frostbite's frame graph (Yuriy O'Donnell, GDC 2017) formalizes it; Destiny and countless others do the same. Vent's `extract_frame` is exactly this.

The reason B is worth the copy: **a snapshot converts a data race into a memcpy.** Once rendering provably reads only frozen data, you can run gameplay for frame N+1 *simultaneously* with rendering frame N, with zero locks. That's the whole game for multithreading. Path A can never get there - it's forever one lock away from a race. Vent isn't *yet* overlapping simulate and render, but the extraction seam is the investment that makes it possible later without re-architecting. This is the single most important structural decision in phase 1, and it looks boring (moving a loop) precisely because good architecture makes hard things look boring.

**Fork 4: How are per-frame GPU uniforms scoped?**

This is the fence bug (G-1) and its fix, generalized. When the CPU writes data the GPU reads (camera matrices), where does that memory live?

- **Path A - One global buffer ring, indexed by frame.** What vent had. Broke the instant two windows had different cameras, because two windows' fences don't coordinate - window B could overwrite a slot window A's GPU work was still reading.
- **Path B - Per-window ring (what vent chose).** Each window's swapchain owns its uniform buffers, written only between *that window's* fence-wait and submit. The storage lives at the scope of the fence that protects it.
- **Path C - Dynamic uniform buffer offsets.** One big buffer, each draw reads at a different offset. Fewer allocations, but you still have to solve the same "which fence proves this region free" problem - it just moves the bookkeeping around.
- **Path D - Bindless / descriptor indexing.** The modern endgame: put everything in giant arrays, index by integer in the shader, stop binding descriptors per draw almost entirely. This is where high-end engines are heading (and vent's roadmap explicitly defers it until per-draw binding has taught the problem it solves).

Vent chose B because it's the direct, comprehensible answer to the actual question - "which fence proves this memory is free?" - and it makes the answer *structural* (the buffer lives inside the object that owns the fence) rather than a rule you have to remember. C and D are optimizations you earn *after* B has made the correctness obvious. Doing D first would be optimizing a binding cost you haven't measured while the correctness model is still fuzzy.

**Fork 5: The threading model itself**

How is parallel work organized? This is a genuine family of choices:

- **Thread-per-subsystem** - a dedicated game thread, render thread, audio thread. This is *classic* Unreal (Game Thread + Render Thread + RHI Thread). Simple to reason about, but scales only to as many cores as you have subsystems, and inter-thread handoff is manual and rigid.
- **Fork-join** - split a loop across workers, join. `parallel_for` is this. Great for data-parallel bulk work (updating 10,000 particles), poor for irregular dependent tasks.
- **Task/job graph with work-stealing** - many small jobs in queues, idle workers steal from busy ones. This is vent's job system (Chase-Lev deques) and modern Unreal's task graph. Scales to any core count, load-balances automatically, but is *hard* to make correct (vent's deque and queue are hand-rolled lock-free structures - powerful and, as the review flagged, currently untested).
- **Fibers** - Naughty Dog's approach (Christian Gyrling, GDC 2015): jobs run on fibers that can be suspended and migrated between threads, so a job can "block" on another job without blocking the OS thread. The most powerful and the most complex; overkill for vent's scale.

Vent chose the job-graph-with-work-stealing model, which is the right ceiling for "utilize every core" - but the honest state is that it's used for a few things (parallel init, command recording) and the lock-free primitives underneath it have no tests. That's the biggest risk in the codebase, and it's why the roadmap puts a test rig + thread sanitizer as the standing highest-value task. The meta-lesson: **choosing the powerful concurrency model obligates you to test it**, because lock-free code that's "correct by luck" stays wrong for years.

---
**Part 4 - The single thread connecting every one of these decisions**

If you step back, every choice above is the same choice made in different domains:

| domain | rejected (imperative/shared/eager) | chosen (declarative/owned/deferred) |
| :--- | :--- | :--- |
| window->surface sync | event callback on a worker | poll-reconcile on main thread |
| frame ordering | undefined / hardcoded | explicit sortable phases |
| control flow | client drives the loop | engine owns the loop |
| scene description | immediate draw calls | retained components |
| render data access | read live world | extracted snapshot |
| uniform memory | one global ring | per-fence-scoped ring |
| resource identity (coming) | raw pointers across threads | generation-checked handles |

The unifying principle - and it's the mature version of your own "async-only" rule - is:

> **Make data safe for concurrency by design (immutable snapshots, single owners, handles, fence-scoped storage), then apply parallelism to data that was designed for it. Prefer converging to a source of truth at a known safe point over reacting to events on unknown threads.**

Your `async-only` principle, taken naively, produces exactly the bugs the review found: parallelism *bolted onto* data structures that were built single-threaded (plain maps, raw pointers, one global buffer). The evolved version isn't a retreat from the principle - it's how the principle survives contact with a real codebase. It's perfectly professional for the world, the frame loop, and the registry-after-init to be single-threaded *by contract*, while the genuinely parallel parts (recording, asset IO, decompression) operate on data handed to them with clear ownership. Every phase-1 decision was shrinking the shared-mutable surface so that when you *do* scale the parallelism back up, it lands on data that was designed to take it.

That's the "why" underneath all the individual whys.

---
A few notes on process, since I saw the settings and instructions change:

- Your `CLAUDE.md` now says the handbook lives at `/docs/handbook.md` and must be updated with every code change. Right now the handbook is at `artifacts/20260707_0024_claude_vent_handbook.md`, and `AGENTS.md` points there. That's an inconsistency worth resolving - want me to **move the handbook to `docs/handbook.md`** and update the pointer in `AGENTS.md` so the two instructions agree? I didn't do it unprompted since it's a structural/naming decision.
- This explanation itself is the kind of thing this project saves. If it's useful, I'm happy to write it into a `docs/design-rationale.md` (or an artifact) so it's a permanent companion to the handbook rather than living only in this conversation. Say the word.

`Churned for 3m 2s`