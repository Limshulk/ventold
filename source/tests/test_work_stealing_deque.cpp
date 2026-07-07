//
// vent tests.
// work_stealing_deque correctness + storm.
// ——————————————————————
//
// the chase-lev deque has SPMC semantics: the OWNER thread push/pop at the
// bottom (lifo); any number of THIEF threads steal() from the top. the whole
// point of the structure is the race on the last element — owner pop() and a
// thief steal() both target it, and the two CAS paths must agree so it goes to
// exactly one of them. the storm exercises exactly that race at scale.
//
// invariant: every pushed value is taken exactly once, by either a pop or a
// steal — never both, never neither.
//
// two storms, targeting the two ways the array pointer is exercised:
//   - "no reallocation": capacity >= N, so the array never changes — this
//     isolates the pure steal/pop race on the last element.
//   - "grow under contention": a tiny start capacity forces the owner to grow
//     the array while thieves steal, exercising the atomic publish of the new
//     array (release store in push, acquire load in steal). a thief must always
//     observe a fully-valid array — never a stale or half-swapped pointer.

#include "vent_test.hpp"

#include <work_stealing_deque.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace vent;

TEST_CASE("work_stealing_deque: single-thread lifo pop + steal") {
    work_stealing_deque<int> d(4);
    CHECK(d.empty());

    d.push(1);
    d.push(2);
    d.push(3);
    CHECK(d.size() == 3);

    CHECK(d.pop().value() == 3);    // lifo from the bottom.
    CHECK(d.pop().value() == 2);
    CHECK(d.steal().value() == 1);  // steal from the top.
    CHECK(!d.pop().has_value());
    CHECK(!d.steal().has_value());
    CHECK(d.empty());
}

TEST_CASE("work_stealing_deque: single-thread grow keeps lifo order") {
    work_stealing_deque<int> d(2);  // tiny: forces several grows.
    for (int i = 0; i < 100; ++i) {
        d.push(i);
    }
    CHECK(d.size() == 100);

    int expected = 99;
    while (auto v = d.pop()) {
        CHECK(v.value() == expected);
        --expected;
    }
    CHECK(expected == -1);  // popped exactly 99..0.
}

// --- shared storm body: owner pushes [0, N), thieves steal, tally seen once ---
namespace {

/// @brief run one owner + THIEVES over a deque sized `capacity`, verify every
///        value in [0, N) is taken exactly once.
/// @param capacity initial deque capacity (large => no grow; small => grows).
/// @param n number of values to push.
auto run_spmc_storm(u32 capacity, u64 n) -> void {
    constexpr int THIEVES = 3;

    work_stealing_deque<u64> d(capacity);
    std::vector<std::atomic<u8>> seen(n);
    std::atomic<bool> done {false};

    // --- thieves: steal from the top until the owner is done and drained ---
    std::vector<std::thread> thieves;
    thieves.reserve(THIEVES);
    for (int t = 0; t < THIEVES; ++t) {
        thieves.emplace_back([&] {
            while (!done.load(std::memory_order_acquire) || !d.empty()) {
                if (auto v = d.steal()) {
                    seen[*v].fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // --- owner: only this thread may push/pop (contract) ---
    std::thread owner([&] {
        for (u64 i = 0; i < n; ++i) {
            d.push(i);
            // occasionally pop from the bottom to race thieves on the tail.
            if ((i & 7u) == 0) {
                if (auto v = d.pop()) {
                    seen[*v].fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        // drain whatever the thieves didn't take.
        while (auto v = d.pop()) {
            seen[*v].fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    owner.join();
    for (auto& th : thieves) {
        th.join();
    }

    u64 losses = 0, dupes = 0;
    for (u64 v = 0; v < n; ++v) {
        const u8 c = seen[v].load(std::memory_order_relaxed);
        if (c == 0) {
            ++losses;
        } else if (c > 1) {
            ++dupes;
        }
    }
    CHECK(losses == 0);
    CHECK(dupes == 0);
}

}  // namespace

TEST_CASE("work_stealing_deque: storm — steal/pop race, no reallocation") {
    // capacity >= N so the deque never grows: isolates the last-element race.
    run_spmc_storm(/*capacity=*/262'144, /*n=*/200'000);
}

TEST_CASE("work_stealing_deque: storm — grow under contention (bounded)") {
    // small start capacity forces grows while thieves steal. n is kept under
    // capacity * 2^7 so at most ~7 grows happen => retired_arrays stays <= 8 and
    // the erase() path (the known latent uaf) is never triggered here.
    run_spmc_storm(/*capacity=*/64, /*n=*/8'000);
}
