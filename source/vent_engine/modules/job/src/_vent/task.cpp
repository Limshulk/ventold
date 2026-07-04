// job module.
// task implementation for SDK.
// ——————————————————————
//
// implements the sdk task class methods. these are exported via VENT_API to the
// client sdk

#include <_vent/job/ic_job.hpp>
#include <_vent/accessors.hpp>

#include <job/interfaces/i_job.hpp>
#include <core/types/task_state.hpp>

#include <exception>

namespace vent {

static auto internal_wait(task_state* state) -> void {
    if (!state)
        return;
    if (state->completed.load(std::memory_order_acquire)) {
        return;
    }

    ic_job* j = job();
    if (j && !j->is_fallback()) {
        static_cast<i_job*>(j)->wait_for_state(state);
    } else {
        // fallback job system is always synchronous, so if we reach here
        // and it's not completed, we just spin. but fallback jobs complete
        // immediately.
        while (!state->completed.load(std::memory_order_acquire)) {
            // spin (should not happen with fallback).
        }
    }
}

static auto internal_release(task_state* state) -> void {
    if (!state)
        return;

    ic_job* j = job();
    if (j && !j->is_fallback()) {
        static_cast<i_job*>(j)->release_state(state);
    } else {
        // fallback release logic.
        u32 prev = state->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            delete state;
        }
    }
}

// --- task lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

task::~task() {
    if (!_state) {
        return;
    }

    internal_wait(_state);
    internal_release(_state);
    _state = nullptr;
}

task::task(task&& other) noexcept
    : _state(other._state) {
    other._state = nullptr;
}

auto task::operator=(task&& other) noexcept -> task& {
    if (this != &other) {
        // release current state.
        if (_state) {
            internal_wait(_state);
            internal_release(_state);
        }

        // transfer ownership.
        _state       = other._state;
        other._state = nullptr;
    }
    return *this;
}

// --- queries ---
// —————————————————————————————————————————————————————————————————————————————

auto task::is_complete() const -> bool {
    return _state && _state->completed.load(std::memory_order_acquire);
}

auto task::is_valid() const -> bool {
    return _state != nullptr;
}

// --- synchronization ---
// —————————————————————————————————————————————————————————————————————————————

auto task::wait() -> void {
    internal_wait(_state);
}

// --- internal helpers ---
// —————————————————————————————————————————————————————————————————————————————

auto task::get_result_ptr() -> void* {
    if (!_state) {
        return nullptr;
    }
    return _state->result_ptr;
}

auto task::rethrow_if_exception() -> void {
    if (_state && _state->exception) {
        std::rethrow_exception(_state->exception);
    }
}

}  // namespace vent
