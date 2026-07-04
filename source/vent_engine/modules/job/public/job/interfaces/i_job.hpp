#pragma once
//
// job module.
// engine-faced job system interface.
// ——————————————————————
//
// extends the client-facing job system interface with internal methods.

#include <_vent/job/ic_job.hpp>

namespace vent {

class i_job : public ic_job {
public:
    virtual ~i_job() = default;

    // --- internal state management ---
    // —————————————————————————————————————————————————————————————————————————
    // used by task class implementation.

    /// @brief internal: wait for a task state to complete.
    /// @param state task state to wait on.
    virtual auto wait_for_state(task_state* state) -> void = 0;

    /// @brief internal: release a reference to task state.
    /// @param state task state to release.
    virtual auto release_state(task_state* state) -> void = 0;

    /// @brief internal: submit a job with explicit state tracking.
    /// @param state task state to use for tracking.
    /// @param job_func function to execute.
    /// @param priority job priority.
    virtual auto submit_with_state(task_state* state,
                                   job_fn      job_func,
                                   job_priority priority) -> void = 0;
};

}  // namespace vent
