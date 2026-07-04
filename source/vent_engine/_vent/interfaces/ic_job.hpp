#pragma once
//
// vent public sdk.
// client-facing job system interface.
// ——————————————————————
//
// client-facing interface for job system operations.
// provides the ability to run tasks in the background while maintaining
// high cpu multi-core utilization.
//
// two different types of jobs exist:
//  1. FIRE-AND-FORGET:     - for background tasks that need to be executed, but
//     ic_job::fire(..)       do not require synchronization with the main
//                            thread.
//                          - vent automatically cleans up once the job
//                            finishes.
//
//     example - play a sound effect. we do not need to wait until it is played.
//       job()->fire([] { audio()->play("sfx.wav"); });
//       // done! no need to track or clean up.
//
//  2. TRACKED (via task) - for work that needs to be tracked or awaited.
//     ic_job::submit(..)  - returns a task object with RAII cleanup.
//                         - task auto-frees when destroyed (no manual
//                           cleanup!).
//                         - use get<T>() to retrieve the typed result.
//*    note: if a task is about to go out of scope before completion, the thread
//*    will be blocked until it finishes. this ensures no tasks are abandoned
//*    unintentionally but basically disables async behavior - use with care!
//
//     example - load a texture from disk in the background. we need to wait for
//     it.
//       {
//           auto t = job()->submit([] { return load_texture("tex.png"); });
//           // do other work while loading...
//           while (!t.is_complete()) {
//               update_loading_bar();
//           }
//           auto texture = t.get<texture>();  // blocks until complete, returns
//           result.
//       }  // task auto-cleans up here. no manual free needed!
//
// memory lifecycle:
//   - job struct (128 bytes): freed immediately when worker finishes executing.
//   - task_state (variable): freed when both worker AND task object are done.
//
// important: tasks must not outlive the job system. destroying the job system
// while tasks still exist will cause memory leaks. ensure all tasks are
// destroyed before calling shutdown_job().
//

#include <_vent/vent_sdk.hpp>

#include <functional>
#include <type_traits>

namespace vent {

// --- general types ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief priority levels for job execution. workers will pick high priority
/// jobs first, then normal, then low.
enum class job_priority : u8 {
    low    = 0,  ///< background tasks.
    normal = 1,  ///< default.
    high   = 2,  ///< urgent.
};

/// @brief worker state for real-time monitoring.
enum class worker_state : u8 {
    idle,     ///< worker is sleeping/waiting for work.
    running,  ///< worker is executing a job.
};

// --- opaque task state ---
// —————————————————————————————————————————————————————————————————————————————
// forward declaration only. actual definition is engine-internal.

/// @brief opaque task state. defined internally by the engine.
struct task_state;

// --- function types ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief function type for regular jobs.
/// a job is just a function that takes no arguments and returns nothing.
using job_fn = std::function<void()>;

/// @brief function type for parallel_for iterations.
/// takes the current index as argument.
using parallel_fn = std::function<void(u64)>;

// --- task : raii wrapper for tracked jobs ---
// —————————————————————————————————————————————————————————————————————————————

/// @brief raii wrapper for tracked jobs.
/// auto-frees job resources when destroyed. cannot leak by design.
/// use get<T>() to retrieve the typed result.
///
/// memory lifecycle:
///   - when worker finishes: job struct freed, state.completed = true.
///   - when task destroyed: waits if needed, releases state reference.
///
/// usage:
///   auto t = job()->submit([] { return load_texture("tex.png"); });
///   // option 1: poll-based.
///   while (!t.is_complete()) { do_other_work(); }
///   // option 2: get result (blocks).
///   auto tex = t.get<texture>();
///   // destructor auto-frees - no manual cleanup!

class task final {
public:
    task() = default;
    VENT_NO_COPY(task);

    /// @brief destructor. auto-waits if job not complete, then frees resources.
    VENT_API ~task();

    /// @brief move constructor. transfers ownership to new task.
    VENT_API task(task&& other) noexcept;

    /// @brief move assignment. transfers ownership to this task.
    VENT_API auto operator=(task&& other) noexcept -> task&;

    // --- queries ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief check if task is complete (non-blocking).
    /// @return true if job has finished executing.
    [[nodiscard]]
    VENT_API auto is_complete() const -> bool;

    /// @brief check if this task refers to a valid job.
    /// @return true if task has a valid state pointer.
    [[nodiscard]]
    VENT_API auto is_valid() const -> bool;

    /// @brief implicit bool conversion for validity check.
    explicit operator bool() const { return is_valid(); }

    // --- synchronisation & results ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief block until job completes.
    /// @note calling thread helps execute other jobs while waiting.
    VENT_API auto wait() -> void;

    /// @brief block until job completes and return the result.
    /// @tparam T the expected result type.
    /// @return the result of the job (moved).
    /// @throws rethrows any exception thrown by the job.
    /// @note undefined behavior if T does not match the actual result type.
    template <typename T>
    [[nodiscard]]
    auto get() -> T {
        wait();
        rethrow_if_exception();
        return std::move(*static_cast<T*>(get_result_ptr()));
    }

    /// @brief block until job completes (void version, no result).
    /// @throws rethrows any exception thrown by the job.
    auto get_void() -> void {
        wait();
        rethrow_if_exception();
    }

private:
    friend class ic_job;

    task_state* _state = nullptr;  ///< opaque pointer to engine-internal state.

    /// @brief internal constructor. used by ic_job::make_task().
    /// @param state opaque state pointer.
    explicit task(task_state* state)
        : _state(state) {}

    /// @brief internal: get raw pointer to result storage.
    [[nodiscard]]
    VENT_API auto get_result_ptr() -> void*;

    /// @brief internal: rethrow exception if one occurred during job execution.
    VENT_API auto rethrow_if_exception() -> void;
};

// --- ic_job ---
// —————————————————————————————————————————————————————————————————————————————

class ic_job {
public:
    static constexpr std::string_view system_name = "vent.system.job";

    virtual ~ic_job() = default;

    // --- identification ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief check if the implementation uses the fallback job system.
    /// @return true if this is the fallback, false for modular systems.
    [[nodiscard]]
    virtual auto is_fallback() const -> bool = 0;

    // --- fire-and-forget ---
    // —————————————————————————————————————————————————————————————————————————
    // non-tracked jobs.

    /// @brief submit a fire-and-forget job to be executed asynchronously.
    /// @param job_func function to execute (pointer or lambda). no return value.
    /// @param priority priority level for job scheduling (default: normal).
    virtual auto fire(job_fn       job_func,
                      job_priority priority = job_priority::normal) -> void = 0;

    /// @brief submit multiple fire-and-forget jobs at once. slightly more
    /// efficient than calling fire() individually.
    /// @param job_funcs array of functions to execute. can be different
    /// functions.
    /// @param count number of functions in the array.
    /// @param priority priority level for job scheduling (default: normal).
    /// applies to all jobs.
    virtual auto fire_batch(const job_fn* job_funcs,
                            u64           count,
                            job_priority  priority = job_priority::normal)
        -> void = 0;

    // --- tracked jobs ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief submit a tracked task to be executed asynchronously.
    /// @tparam F callable type (auto-deduced from argument).
    /// @param func function/lambda to execute. return value is stored in task.
    /// @param priority priority level for job scheduling (default: normal).
    /// @return task handle. use get<R>() to retrieve result where R is the
    /// callable's return type.
    ///
    /// example:
    ///   auto t = job()->submit([] { return load_texture("tex.png"); });
    ///   auto tex = t.get<texture>();  // blocks and returns the texture.
    template <typename F>
    auto submit(F&& func, job_priority priority = job_priority::normal) -> task;

    // --- synchronization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief wait for a single task to complete.
    /// note: this is a convenience wrapper; you can also call task.wait()
    /// directly.
    /// @param t the task to wait for.
    auto wait(task& t) -> void { t.wait(); }

    /// @brief wait for multiple tasks to complete (variadic template version).
    /// @param tasks variadic list of task references.
    template <typename... TASKS>
    auto wait_all(TASKS&... tasks) -> void {
        // fold expression: calls wait() on each task.
        (tasks.wait(), ...);
    }

    /// @brief wait for ALL pending jobs to complete (fire-and-forget + tracked).
    /// blocks until every queued job has finished executing.
    /// note: this does NOT free tracked tasks - their destructors handle that.
    virtual auto drain() -> void = 0;

    // --- parallelization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief run a loop across multiple CPU cores at once.
    /// this is like a regular for-loop, but uses all your cores!
    /// blocks until all iterations are complete.
    ///
    /// example - update 1000 enemies:
    ///   // instead of: for (int i = 0; i < 1000; i++) enemies[i].update();
    ///   // use:
    ///   job()->parallel_for(0, 1000, [&](u64 i) {
    ///       enemies[i].update();
    ///   });
    ///   // runs ~8x faster on an 8-core CPU!
    ///
    /// @param begin first index (inclusive).
    /// @param end last index (exclusive).
    /// @param func what to do for each index.
    /// @param chunk_size how many indices per job (0 = auto-detect).
    virtual auto parallel_for(u64         begin,
                              u64         end,
                              parallel_fn func,
                              u64         chunk_size = 0) -> void = 0;

protected:
    /// @brief internal: submit with type-erased result storage.
    /// @param wrapper function that executes and stores result to result_ptr.
    /// @param result_size size of the result type in bytes.
    /// @param result_deleter function to call destructor on the result.
    /// @param priority job priority.
    /// @return task handle.
    virtual auto submit_internal(std::function<void(void*)> wrapper,
                                 usize                      result_size,
                                 void (*result_deleter)(void*),
                                 job_priority priority) -> task = 0;

    /// @brief internal: create a task from opaque state. subclass factory.
    /// @param state opaque state pointer.
    /// @return task handle wrapping the state.
    static auto make_task(task_state* state) -> task {
        return task(state);
    }
};

// --- ic_job::submit template implementation ---
// ——————————————————————————————————————————————————————————————————————————————

template <typename F>
auto ic_job::submit(F&& func, job_priority priority) -> task {
    using R = std::invoke_result_t<F>;

    // create wrapper that calls func and stores result to provided pointer.
    auto wrapper = [f = std::forward<F>(func)](void* result_ptr) mutable {
        if constexpr (std::is_void_v<R>) {
            f();
        } else {
            ::new (result_ptr) R(f());
        }
    };

    // create type-erased deleter for result (calls destructor).
    void (*deleter)(void*) = nullptr;
    if constexpr (!std::is_void_v<R> && !std::is_trivially_destructible_v<R>) {
        deleter = [](void* ptr) {
            static_cast<R*>(ptr)->~R();
        };
    }

    // determine result size (0 for void, sizeof(R) otherwise).
    usize result_size = 0;
    if constexpr (!std::is_void_v<R>) {
        result_size = sizeof(R);
    }

    // submit via internal method.
    return submit_internal(std::move(wrapper), result_size, deleter, priority);
}

}  // namespace vent