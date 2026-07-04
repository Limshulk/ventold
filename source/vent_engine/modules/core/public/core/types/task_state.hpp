#pragma once
//
// core module.
// task state definition.
// ——————————————————————
//
// shared definition of task_state used by both the job module and the
// fallback job system. this struct holds the synchronization state between
// a task object and the worker executing the job.

#include <_vent/vent_sdk.hpp>

#include <atomic>
#include <exception>

namespace vent {

/// @brief internal task state. stores completion status, result data, and
/// reference counting for cleanup.
///
/// memory lifecycle:
///   - created with ref_count = 2 (one for task object, one for
///   worker/fallback).
///   - when worker finishes: decrements ref_count.
///   - when task destructor runs: decrements ref_count.
///   - when ref_count reaches 0: destructor is called, memory freed.
struct task_state {
    std::atomic<bool> completed {false};  ///< true when job finished executing.
    std::atomic<u32>  ref_count {2};      ///< reference count (task + worker).

    void* result_ptr {nullptr};               ///< pointer to result storage.
    usize result_size {0};                    ///< size of result in bytes.
    void (*result_deleter)(void*) {nullptr};  ///< destructor for result type.

    std::exception_ptr exception {nullptr};  ///< exception if job threw.

    // --- job module internals (unused by fallback) ---

    u64   id {0};             ///< unique job identifier.
    u64   batch_id {0};       ///< batch id (0 = single job).
    void* job_ptr {nullptr};  ///< internal job pointer for job_system.

    /// @brief destructor. frees result storage if allocated.
    ~task_state() {
        if (result_ptr) {
            // call destructor if we have one.
            if (result_deleter) {
                result_deleter(result_ptr);
            }
            // free the memory.
            ::operator delete(result_ptr);
            result_ptr = nullptr;
        }
    }
};

}  // namespace vent
