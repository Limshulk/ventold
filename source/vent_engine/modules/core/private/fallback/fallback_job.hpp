#pragma once
//
// core module.
// fallback ic_job serializer.
// ——————————————————————
//
// provides a null-object ic_job implementation for when the job module isn't
// linked (why would you do that?) or initialized yet. all jobs execute
// immediately inline on the calling thread.
// intentionally header-only.
//
// this ensures code can always call job()->fire(...) without null checks,
// and the engine can use async-style code patterns regardless of whether
// the job system is actually available.
//
// usage:
//   - the job() accessor automatically returns this fallback when the
//     real job system isn't available.
//   - code calls job()->fire(...) uniformly, execution adapts automatically.

#include <_vent/job/ic_job.hpp>

#include <core/types/task_state.hpp>

namespace vent {

/// @brief null-object job system that executes all jobs immediately inline.
///        used when the job module isn't linked, or before bootstrap completes.
class fallback_job final : public ic_job {
private:
    fallback_job()           = default;
    ~fallback_job() override = default;

    VENT_NO_COPY_MOVE(fallback_job);
public:
    // --- singleton access ---
    // —————————————————————————————————————————————————————————————————————————
    // the fallback job is a singleton - statically allocated in the core module
    // and always available.

    /// @brief get the singleton fallback job instance.
    /// @return reference to the global fallback_job instance.
    static auto instance() -> fallback_job& {
        static fallback_job s_instance;
        return s_instance;
    }

    // --- ic_job implementation ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief identify this as the fallback job system.
    [[nodiscard]]
    auto is_fallback() const -> bool override {
        return true;
    }

    /// @brief execute a job immediately inline (no async).
    /// affinity is irrelevant here: with no worker threads, "inline on the
    /// caller" is the only place a job can run.
    auto fire(job_fn func,
              job_priority /*priority*/ = job_priority::normal,
              job_affinity /*affinity*/ = job_affinity::any) -> void override {
        if (func) {
            func();
        }
    }

    /// @brief execute multiple jobs immediately inline (no async).
    auto fire_batch(const job_fn* funcs,
                    u64           count,
                    job_priority /*priority*/ = job_priority::normal)
        -> void override {
        if (!funcs)
            return;
        for (u64 i = 0; i < count; ++i) {
            if (funcs[i]) {
                funcs[i]();
            }
        }
    }

    /// @brief drain pending jobs. no-op since nothing is ever pending.
    auto drain() -> void override {
        // nothing to drain - all jobs execute inline.
    }

    /// @brief run jobs pinned to the calling thread. no-op: the fallback has no
    /// inbox because every job already ran inline at submit time.
    auto run_pinned_jobs() -> void override {
        // nothing to run - all jobs execute inline.
    }

    /// @brief execute parallel_for as a sequential loop.
    auto parallel_for(u64         begin,
                      u64         end,
                      parallel_fn func,
                      u64 /*chunk_size*/ = 0) -> void override {
        if (!func)
            return;
        for (u64 i = begin; i < end; ++i) {
            func(i);
        }
    }

protected:
    /// @brief submit with immediate execution. creates task_state, runs job,
    /// returns completed task.
    auto submit_internal(std::function<void(void*)> wrapper,
                         usize                      result_size,
                         void (*result_deleter)(void*),
                         job_priority /*priority*/,
                         job_affinity /*affinity*/) -> task override {
        // allocate task state.
        auto* state = new task_state();
        state->ref_count.store(2,
                               std::memory_order_relaxed);  // task + "worker".
        state->result_size    = result_size;
        state->result_deleter = result_deleter;

        // allocate result storage if needed.
        if (result_size > 0) {
            state->result_ptr = ::operator new(result_size);
        }

        // execute the wrapper immediately.
        try {
            wrapper(state->result_ptr);
        } catch (...) {
            state->exception = std::current_exception();
        }

        // mark as complete.
        state->completed.store(true, std::memory_order_release);

        // release "worker" reference (task object holds the other).
        // since we're returning the task, it will decrement when destroyed.
        u32 prev = state->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            // shouldn't happen since we're returning a task, but handle it.
            delete state;
            return make_task(nullptr);
        }

        return make_task(state);
    }
};

}  // namespace vent
