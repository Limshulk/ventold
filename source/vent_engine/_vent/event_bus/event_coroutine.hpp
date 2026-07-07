#pragma once
//
// vent public sdk.
// event_coroutine — co_await an event.
// ——————————————————————
//
// bridges the event bus (<_vent/event_bus/ic_event_bus.hpp>) to coroutines: a
// co_task can `co_await await_event("name")` to suspend until that event fires,
// then resume — by default on the main thread, at the frame-start drain.
//
// this is events used as a SYNCHRONIZATION primitive (suspend-until-fired), not
// just notification (a pre-registered callback). it is built entirely on top of
// subscribe() + the delivery policies (event_delivery): the awaiter registers a
// one-shot subscription whose callback resumes the suspended coroutine on the
// chosen delivery thread.
//
// usage:
//   auto react() -> vent::co_task {
//       kick_off_parallel_work();
//       void* data = co_await vent::await_event("physics.done");  // suspends.
//       // resumes here on the main thread once "physics.done" is published.
//       spawn_debris();
//   }

#include <_vent/vent_sdk.hpp>
#include <_vent/accessors.hpp>
#include <_vent/core/co_task.hpp>
#include <_vent/event_bus/ic_event_bus.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace vent {

/// @brief awaiter returned by await_event(). suspends the awaiting coroutine and
/// resumes it, once, when the named event next fires.
///
/// lifetime note: the awaiter lives on the coroutine frame across the
/// suspension. the resume callback captures only trivially-copyable state (the
/// coroutine handle) plus a shared payload slot — never the awaiter itself — so
/// it stays valid even though the frame is destroyed the moment the coroutine
/// completes on resume.
class event_awaiter {
public:
    /// @param event event name to await.
    /// @param resume_on delivery policy for the resume (default: main thread).
    event_awaiter(std::string event, event_delivery resume_on)
        : _event(std::move(event)),
          _resume_on(resume_on) {}

    /// @brief never ready: awaiting an event always suspends and waits.
    [[nodiscard]]
    auto await_ready() const noexcept -> bool {
        return false;
    }

    /// @brief register a one-shot subscription that resumes the coroutine when
    /// the event fires, on the chosen delivery thread.
    auto await_suspend(std::coroutine_handle<> handle) -> void {
        auto* bus = event();

        // a shared id slot lets the callback unsubscribe itself (one-shot). a
        // shared payload slot carries the event data back to await_resume().
        auto id      = std::make_shared<std::atomic<subscription_id>>(
            INVALID_SUBSCRIPTION);
        auto payload = _payload;

        subscription_id sid = bus->subscribe(
            _event,
            [handle, id, bus, payload](std::string_view, void* data) {
                payload->store(data, std::memory_order_relaxed);
                // one-shot: remove ourselves before resuming. safe — dispatch
                // iterates a snapshot, so erasing during a callback is fine.
                bus->unsubscribe(id->load(std::memory_order_relaxed));
                // resume the coroutine on THIS (the delivery) thread. with the
                // default resume_on == main, that is the main-thread frame drain.
                handle.resume();
            },
            _resume_on);

        // store the id for the callback. with the default main-delivery resume,
        // the callback cannot run before the next frame, so this store always
        // happens first. (for parallel/immediate resume a concurrent publish
        // could in theory fire before this store — a documented v1 edge; main is
        // the default and is race-free.)
        id->store(sid, std::memory_order_relaxed);
    }

    /// @brief the event payload captured when it fired (may be nullptr).
    /// not [[nodiscard]]: awaiting purely for the signal, ignoring the payload,
    /// is a common and valid use (`co_await await_event("x");`).
    auto await_resume() const noexcept -> void* {
        return _payload->load(std::memory_order_relaxed);
    }

private:
    std::string                         _event;      ///< event to await.
    event_delivery                      _resume_on;  ///< thread to resume on.
    std::shared_ptr<std::atomic<void*>> _payload =   ///< event data on fire.
        std::make_shared<std::atomic<void*>>(nullptr);
};

/// @brief await an event inside a co_task. suspends until `event` next fires,
/// then resumes on `resume_on` and returns the event's payload pointer.
/// @param event event name to await.
/// @param resume_on delivery policy for the resume (default: main thread — the
/// safe choice, since the resumed code typically touches main-owned state).
/// @return an awaiter; use as `co_await await_event(...)`.
[[nodiscard]]
inline auto await_event(std::string_view event,
                        event_delivery   resume_on = event_delivery::main)
    -> event_awaiter {
    return event_awaiter(std::string(event), resume_on);
}

}  // namespace vent
