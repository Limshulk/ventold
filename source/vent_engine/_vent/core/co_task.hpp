#pragma once
//
// vent public sdk.
// co_task — a detached coroutine.
// ——————————————————————
//
// co_task is the return type of a function that wants to `co_await` (e.g. an
// event, see <_vent/event_bus/event_coroutine.hpp>). it is fire-and-forget: the
// coroutine starts running immediately, runs until its first suspension point,
// and is resumed later by whatever it awaited. when it finally returns, its
// frame is freed automatically — nothing to hold, nothing to join.
//
// this is deliberately the *simplest* coroutine type: no result, no handle, no
// cancellation. it is the substrate for "run in parallel, sync on fired events"
// — a system does work, co_awaits a signal, and continues when it fires, without
// blocking a thread while waiting.
//
// usage:
//   auto react() -> vent::co_task {
//       do_some_work();
//       co_await vent::await_event("physics.done");  // suspends here.
//       do_more_work();                               // resumes when fired.
//   }
//   react();  // starts immediately; returns as soon as it hits the co_await.

#include <_vent/vent_sdk.hpp>

#include <coroutine>
#include <cstdio>

namespace vent {

/// @brief a detached, fire-and-forget coroutine.
/// starts eagerly, suspends at the first co_await, resumes when that awaitable
/// completes, and self-destructs when it returns.
struct co_task {
    struct promise_type {
        /// @brief the object handed back to the caller. co_task carries no
        /// state — the coroutine is detached — so this is empty.
        [[nodiscard]]
        auto get_return_object() noexcept -> co_task {
            return {};
        }

        /// @brief start running immediately (do work up to the first co_await),
        /// rather than suspending at the top.
        [[nodiscard]]
        auto initial_suspend() noexcept -> std::suspend_never {
            return {};
        }

        /// @brief free the frame automatically on completion. because we never
        /// suspend at the end, the coroutine handle must not be touched after
        /// the coroutine returns — there is no owner and nothing to resume.
        [[nodiscard]]
        auto final_suspend() noexcept -> std::suspend_never {
            return {};
        }

        auto return_void() noexcept -> void {}

        /// @brief an exception escaping a detached coroutine has nowhere to
        /// propagate (no caller is awaiting it). report it loudly rather than
        /// calling std::terminate and taking down the whole engine. note: we
        /// use fprintf, not log(), to keep this type free of engine
        /// dependencies so it stays unit-testable in isolation.
        auto unhandled_exception() noexcept -> void {
            std::fprintf(stderr, "[vent] unhandled exception in co_task.\n");
        }
    };
};

}  // namespace vent
