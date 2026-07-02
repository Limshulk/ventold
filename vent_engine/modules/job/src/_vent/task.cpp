// job module.
// task implementation for SDK.
// ——————————————————————
//
// implements the sdk task class methods. these are exported via VENT_API to the
// client sdk

#include <_vent/interfaces/ic_job.hpp>
#include <_vent/accessors.hpp>

#include <job/interfaces/i_job.hpp>
#include <core/types/task_state.hpp>

#include <exception>

namespace vent {

static auto get_i_job() -> i_job* {
    //! THIS MAY RESULT IN CRASHES.
    // if the job system isn't initialized, this cast will be invalid.
    // job() will return the fallback job system, which does NOT implement i_job.
    // todo: FIX!
    return static_cast<i_job*>(job());
}

// --- task lifecycle ---
// —————————————————————————————————————————————————————————————————————————————

task::~task() {
    if (!_state) {
        return;
    }

    auto* jobs = get_i_job();
    if (!jobs) {
        _state = nullptr;
        return;
    }

    // wait for completion if not done.
    if (!_state->completed.load(std::memory_order_acquire)) {
        jobs->wait_for_state(_state);
    }

    // release our reference.
    jobs->release_state(_state);
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
            auto* jobs = get_i_job();
            if (jobs) {
                if (!_state->completed.load(std::memory_order_acquire)) {
                    jobs->wait_for_state(_state);
                }
                jobs->release_state(_state);
            }
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
    if (!_state) {
        return;
    }

    // fast path: already complete.
    if (_state->completed.load(std::memory_order_acquire)) {
        return;
    }

    // wait via job system.
    auto* jobs = get_i_job();
    if (jobs) {
        jobs->wait_for_state(_state);
    }
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
