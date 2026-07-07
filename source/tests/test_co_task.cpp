//
// vent tests.
// co_task coroutine machinery.
// ——————————————————————
//
// tests the co_task type in isolation (no engine, no event bus): does it start
// eagerly, suspend at a co_await, and resume correctly? the awaiter here is a
// manual test double that captures the coroutine handle so the test can drive
// the resume itself — the same shape event_awaiter uses, minus the event bus.
//
// the full await_event() path (co_task + delivery policies) is exercised at
// runtime by the minimal testbed; here we prove the coroutine plumbing.

#include "vent_test.hpp"

#include <_vent/core/co_task.hpp>

#include <coroutine>

using namespace vent;

namespace {

/// @brief a test awaiter: never ready, hands its coroutine handle to `*out` on
/// suspend so the test can resume it on demand.
struct manual_awaiter {
    std::coroutine_handle<>* out;

    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> h) noexcept -> void {
        *out = h;
    }
    auto await_resume() const noexcept -> void {}
};

/// @brief coroutine body. parameters are copied into the frame (pointers), so
/// there is no dangling-capture hazard — unlike a capturing-lambda coroutine.
auto co_body(int* stage, std::coroutine_handle<>* out) -> co_task {
    *stage = 1;                       // runs eagerly (initial_suspend = never).
    co_await manual_awaiter {out};    // suspends; hands the handle to the test.
    *stage = 2;                       // runs when the test resumes it.
}

}  // namespace

TEST_CASE("co_task: runs eagerly, suspends at co_await, resumes to completion") {
    int                     stage = 0;
    std::coroutine_handle<> saved {};

    co_body(&stage, &saved);

    // it ran eagerly up to the co_await, then suspended.
    CHECK(stage == 1);
    REQUIRE(saved != nullptr);  // a handle was captured on suspend.
    CHECK(!saved.done());       // suspended, not finished.

    // drive the resume.
    saved.resume();

    // it continued past the co_await to the end.
    CHECK(stage == 2);
    // frame is self-destroyed on completion (final_suspend = suspend_never), so
    // `saved` now dangles — do NOT touch it again.
}

TEST_CASE("co_task: a coroutine with no suspension runs fully and returns") {
    int stage = 0;

    // no co_await inside: with initial_suspend = suspend_never it runs straight
    // through and frees itself before the call even returns.
    auto body = [](int* s) -> co_task {
        *s = 42;
        co_return;
    };
    body(&stage);

    CHECK(stage == 42);
}
