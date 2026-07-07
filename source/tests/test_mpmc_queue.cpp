//
// vent tests.
// mpmc_queue correctness + storm.
// ——————————————————————
//
// mpmc_queue is a bounded lock-free multi-producer / multi-consumer queue
// (vyukov). the storm test is the important one: many producers and many
// consumers hammer a small ring, and we assert the fundamental invariant of any
// queue — every produced item is consumed EXACTLY once (no loss, no
// duplication). unique per-item tags + a per-item seen-count make both failure
// modes detectable.
//
// note: on a debug/-O0 msvc-or-mingw build this mainly proves functional
// correctness and freedom from deadlock/crashes. the memory-ordering bugs this
// structure could hide are best surfaced by building with ThreadSanitizer on
// the linux preset (-DVENT_TESTS_TSAN=ON).

#include "vent_test.hpp"

#include <core/containers/mpmc_queue.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace vent;

TEST_CASE("mpmc_queue: single-thread fifo, full and empty") {
    mpmc_queue<int> q(4);
    CHECK(q.capacity() == 4);
    CHECK(!q.try_pop().has_value());  // empty.

    CHECK(q.try_push(1));
    CHECK(q.try_push(2));
    CHECK(q.try_push(3));
    CHECK(q.try_push(4));
    CHECK(!q.try_push(5));  // full: capacity is 4.

    CHECK(q.try_pop().value() == 1);  // fifo order.
    CHECK(q.try_pop().value() == 2);
    CHECK(q.try_pop().value() == 3);
    CHECK(q.try_pop().value() == 4);
    CHECK(!q.try_pop().has_value());  // empty again.
}

TEST_CASE("mpmc_queue: storm — no loss, no duplication") {
    constexpr int PRODUCERS    = 4;
    constexpr int CONSUMERS    = 4;
    constexpr u64 PER_PRODUCER = 50'000;
    constexpr u64 TOTAL        = static_cast<u64>(PRODUCERS) * PER_PRODUCER;

    // a small ring relative to TOTAL, so producers hit "full" and consumers hit
    // "empty" constantly — maximizing contention on the cursors.
    mpmc_queue<u64> q(1024);

    // seen[v] counts how many times value v was popped. must end at exactly 1.
    // atomic<u8> value-initializes to 0 (c++20); the vector size-ctor needs no
    // moves, so non-movable atomics are fine here.
    std::vector<std::atomic<u8>> seen(TOTAL);

    std::atomic<u64> consumed {0};

    // --- producers: each emits a disjoint, unique value range ---
    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            const u64 base = static_cast<u64>(p) * PER_PRODUCER;
            for (u64 i = 0; i < PER_PRODUCER; ++i) {
                const u64 value = base + i;
                // spin until the (bounded) ring accepts the item.
                while (!q.try_push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // --- consumers: pop until the global count reaches TOTAL ---
    for (int c = 0; c < CONSUMERS; ++c) {
        threads.emplace_back([&] {
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (auto item = q.try_pop()) {
                    seen[*item].fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    CHECK(consumed.load() == TOTAL);

    // tally losses (seen == 0) and duplicates (seen > 1) in one pass, so we emit
    // two CHECKs instead of TOTAL of them.
    u64 losses = 0, dupes = 0;
    for (u64 v = 0; v < TOTAL; ++v) {
        const u8 n = seen[v].load(std::memory_order_relaxed);
        if (n == 0) {
            ++losses;
        } else if (n > 1) {
            ++dupes;
        }
    }
    CHECK(losses == 0);
    CHECK(dupes == 0);
}
