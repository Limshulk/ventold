//
// vent tests.
// circular_array correctness.
// ——————————————————————
//
// circular_array is single-threaded by contract (it backs the lock-free
// structures, which provide their own synchronization). so these are plain
// correctness checks: power-of-two rounding, index wrap, and grow().

#include "vent_test.hpp"

#include <core/containers/circular_array.hpp>

using namespace vent;

TEST_CASE("circular_array: capacity rounds up to a power of two") {
    circular_array<int> a(10);
    CHECK(a.capacity() == 16);  // 10 -> next pow2 = 16.

    circular_array<int> b(16);
    CHECK(b.capacity() == 16);  // already a power of two.

    circular_array<int> c(1);
    CHECK(c.capacity() == 1);  // 1 == 2^0, mask 0, single-slot array.
}

TEST_CASE("circular_array: put/get wrap around the mask") {
    circular_array<int> a(4);  // slots 0..3.

    // write 0..15; each slot keeps the last value written to it.
    for (u64 i = 0; i < 16; ++i) {
        a.put(i, static_cast<int>(i));
    }

    // slot i&3 last saw value 12+i (the final full pass).
    CHECK(a.get(0) == 12);
    CHECK(a.get(1) == 13);
    CHECK(a.get(2) == 14);
    CHECK(a.get(3) == 15);
    CHECK(a.get(4) == 12);  // index 4 wraps to slot 0.
}

TEST_CASE("circular_array: grow preserves the live range") {
    circular_array<int> a(4);
    for (u64 i = 4; i < 8; ++i) {
        a.put(i, static_cast<int>(i));
    }

    auto bigger = a.grow(4, 8);  // copy the [4, 8) range into a 2x array.
    CHECK(bigger->capacity() == 8);
    for (u64 i = 4; i < 8; ++i) {
        CHECK(bigger->get(i) == static_cast<int>(i));
    }
}
