//
// vent tests.
// mpsc_queue correctness + storm.
// ——————————————————————
//
// mpsc_queue is a bounded lock-free multi-producer / SINGLE-consumer queue. the
// storm honors that contract: many producer threads, exactly one consumer
// thread. same invariant as mpmc — every produced item consumed exactly once.
//
// note: pop() spin-waits when a slot is reserved-but-not-yet-written, so it must
// never be called on a truly empty queue from a hot loop; the storm uses
// try_pop() (non-blocking) for draining and reserves pop() for the
// known-non-empty single-thread checks.

#include "vent_test.hpp"

#include <core/containers/mpsc_queue.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace vent;

TEST_CASE("mpsc_queue: single-thread order, size, full and empty") {
    mpsc_queue<int> q(4);
    CHECK(q.capacity() == 4);
    CHECK(q.empty());
    CHECK(!q.try_pop().has_value());

    CHECK(q.push(10));
    CHECK(q.push(20));
    CHECK(q.size() == 2);

    CHECK(q.try_pop().value() == 10);  // fifo.
    CHECK(q.pop().value() == 20);      // known non-empty: pop() is safe here.
    CHECK(q.empty());
    CHECK(!q.try_pop().has_value());
}

TEST_CASE("mpsc_queue: storm — many producers, single consumer") {
    constexpr int PRODUCERS    = 6;
    constexpr u64 PER_PRODUCER = 50'000;
    constexpr u64 TOTAL        = static_cast<u64>(PRODUCERS) * PER_PRODUCER;

    mpsc_queue<u64> q(1024);

    // seen[v] must end at exactly 1 for every produced value.
    std::vector<std::atomic<u8>> seen(TOTAL);

    // --- producers: disjoint value ranges, retry on full ---
    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            const u64 base = static_cast<u64>(p) * PER_PRODUCER;
            for (u64 i = 0; i < PER_PRODUCER; ++i) {
                while (!q.push(base + i)) {
                    std::this_thread::yield();  // ring full — let the consumer drain.
                }
            }
        });
    }

    // --- single consumer: drain until TOTAL items collected ---
    std::thread consumer([&] {
        u64 count = 0;
        while (count < TOTAL) {
            if (auto item = q.try_pop()) {
                seen[*item].fetch_add(1, std::memory_order_relaxed);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

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
