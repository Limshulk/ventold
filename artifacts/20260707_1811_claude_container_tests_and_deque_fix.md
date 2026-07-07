# container storm-test rig + work-stealing deque atomic-publish fix

**date:** 2026-07-07 · **scope:** `source/tests/` (new), `work_stealing_deque.hpp`,
build wiring, handbook. **companion:** `20260707_1727_claude_event_system_rethink.md`
(the design discussion this work fell out of — step 0 of that proposal).

---

## 1. what was missing

The three hand-rolled lock-free containers (`mpmc_queue`, `mpsc_queue`,
`work_stealing_deque`) and their single-threaded backing store (`circular_array`) had
**no tests**. The handbook itself called untested lock-free code "the single scariest fact
in the codebase." There was no test target, no ctest registration, and no way to run the
structures under a sanitizer.

## 2. what was built

### a self-contained test rig (`source/tests/`)

- **`vent_test.hpp`** — an ~80-line harness with `doctest`-shaped macros
  (`TEST_CASE` / `CHECK` / `REQUIRE`), thread-safe assertion recording (`CHECK` is callable
  from worker threads), and a summary runner. Deliberately **not** doctest/catch2: no
  FetchContent means the test target configures and builds **offline**, which matters for a
  reproducible ci storm rig. Macro names mirror doctest so swapping later is mechanical.
- **`test_circular_array.cpp`** — single-threaded correctness (power-of-two rounding, index
  wrap, `grow` range copy). This type is single-threaded by contract.
- **`test_mpmc_queue.cpp` / `test_mpsc_queue.cpp`** — single-threaded correctness + storm
  tests. Each storm has producers emit **disjoint, unique value ranges** into a deliberately
  undersized ring and consumers drain it; a per-value seen-count then proves the queue's
  fundamental invariant — **every item consumed exactly once** (seen == 0 ⇒ loss,
  seen > 1 ⇒ duplication). mpmc uses 4 producers × 4 consumers; mpsc uses 6 producers × 1
  consumer (honoring its single-consumer contract).
- **`test_work_stealing_deque.cpp`** — single-threaded lifo + grow, plus two SPMC storms:
  one sized so the array **never reallocates** (isolates the last-element steal/pop race),
  one that **forces grows under contention** (exercises the array publish).

### build wiring

- `source/tests/CMakeLists.txt` builds the `vent_tests` executable (header-only containers
  ⇒ links no engine module, just include dirs + `Threads::Threads`), registers it with
  ctest, and keeps it out of the shipped sdk tree.
- Root `CMakeLists.txt` gates it behind `option(VENT_BUILD_TESTS ON)` + `enable_testing()`,
  so a normal `VENT_ALL` build stays lean and tests are opt-in via `--target vent_tests`.
- **`-DVENT_TESTS_TSAN=ON`** adds ThreadSanitizer on the linux gcc/clang preset (mingw has
  no tsan, so it warns and ignores). tsan is where the memory-ordering bugs these structures
  can hide actually surface; the windows build proves functional correctness + freedom from
  deadlock/crashes.

Run: `cmake --build --preset windows-debug --target vent_tests` then the exe in
`build/cmake-windows-debug/tests/`, or `ctest`.

## 3. the bug the rig caught, and the fix

The grow-under-contention deque storm **segfaulted intermittently** (~1 run in 4–10). The
no-reallocation storm — identical steal/pop pressure but no array switch — never crashed.
That natural experiment pinned the fault to the grow path:

```cpp
// old push() grow path:
_retired_arrays.push_back(std::move(_array));  // _array is now a moved-from null unique_ptr
_array = std::move(new_array);                 // ...until the next line
```

`_array` was a plain `unique_ptr` the **owner reassigned during grow**, while **thieves read
`_array.get()` in `steal()` with no synchronization**. Between the two lines above, `_array`
is null; a thief that acquire-nothing-loads it there dereferences null. Keeping old *buffers*
alive in `_retired_arrays` never helped — the **pointer swap itself** was the race, not the
buffer lifetime. (The old `erase()` when `_retired_arrays.size() > 8` was a second latent
hazard: it could free an array a thief still pointed into.)

**Fix — canonical chase-lev array publish:**

- `_array` is now `std::atomic<circular_array<T>*>`. The owner **release-stores** the fully
  built new array in `push`; thieves **acquire-load** it in `steal`. There is no null or
  half-swapped window — a thief always observes a complete, valid array, and the acquire/
  release pair makes the copied elements visible.
- A single owner-only `std::vector<unique_ptr<circular_array<T>>> _arrays` **retains every
  array for the deque's lifetime**. Because growth is geometric (doubling), total retained
  memory is bounded to ~2× the final array — cheap — and it removes the reclamation-while-
  stealing problem entirely, with no need for hazard pointers or epoch reclamation at this
  scale. The owner-only `pop`/`push` loads of `_array` stay relaxed (single writer).

**Result:** 60/60 stress runs green (was ~1-in-4-to-10 crashing). Full engine rebuilds
clean against the new deque (job system links unchanged).

## 4. the message

- **A concurrency structure is only as correct as its test rig can prove.** This bug had
  presumably shipped fine for a while because nothing hammered the grow path with real
  thieves. The storm found it on the *first night* it existed. Untested lock-free code isn't
  "probably fine" — it's "unmeasured."
- **Isolate the variable.** Two storms that differ in exactly one axis (does the array
  reallocate?) turned "it segfaults sometimes" into "the array *pointer swap* is the race"
  in one run. Design storms to bisect, not just to stress.
- **Publish, don't reassign.** Any state a lock-free reader touches must change via a single
  atomic publish with the right release/acquire pairing — never a multi-step mutation of a
  non-atomic member, however brief the window. "Brief" is exactly when it bites, rarely.
- **Reclamation is the hard half of lock-free.** Freeing memory a concurrent reader might
  still hold is the deep problem (hazard pointers, epochs, RCU exist for it). When the data's
  growth is bounded and geometric, *just keep it all alive* — the boring, correct, KISS
  answer beats a clever reclaimer you'd have to test even harder.

## 5. state after this session

- `vent_tests`: 11 cases / 154 checks, deterministically green; deque stress 60/60.
- Handbook updated (§9.1 containers, threading rule 7, §12 task map, §13 limitations) to
  describe the current design — atomic array publish, lifetime-retained arrays, the test
  target — with no bug/timeline narrative (that history lives here, in this artifact).
- Nothing committed. Standing next steps (from the companion doc): wire `vent_tests` into ci
  with the tsan build; then the event-system delivery-policy work (steps 1–3).
