#pragma once
//
// job module.
// job_system implementation.
// ——————————————————————
//
// job system using lock-free work-stealing deques.
// todo: write description.

#include <job/interfaces/i_job.hpp>

#include <work_stealing_deque.hpp>

#include <core/types/task_state.hpp>
#include <core/containers/mpmc_queue.hpp>
#include <core/interfaces/ir_bootstrap.hpp>

#include <_vent/system/system_base.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vent {

class job_system final
    : public system_base
    , public i_job
    , public ir_bootstrap {
public:
    job_system() = default;
    ~job_system() override;

    VENT_NO_COPY_MOVE(job_system);
public:
    // --- system interface implementation ---
    // —————————————————————————————————————————————————————————————————————————
    // job_system is a bootstrap system.

    [[nodiscard]]
    auto name() const -> std::string_view override {
        return ic_job::system_name;
    }

    [[nodiscard]]
    auto on_initialization(i32 stage = 0)
        -> system_initialization_result override {
        return initialize() ? system_initialization_result::complete()
                            : system_initialization_result::failed();
    }

    auto on_shutdown() -> void override { shutdown(); }

private:
    // --- internal types ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief lifecycle states for jobs. for tracked tasks, completion is
    /// signaled via task_state::completed atomic flag, not via this enum. this
    /// enum is used for fire-and-forget job cleanup.
    enum class job_lifecycle : u8 {
        pending = 0,  ///< job is queued or in progress.
        done    = 1   ///< job execution complete. cleanup can proceed.
    };

    /// @brief internal job representation. alignment prevents false sharing
    /// between threads. without, multiple threads / cores accessing different
    /// jobs could end up on the same cache line, causing performance killing
    /// cache bounces.
    struct alignas(CACHE_LINE) job_t {
        job_fn       func;      ///< 0x00-0x1F (32b): function to execute.
        job_priority priority;  ///< 0x20 (1b): execution priority.
                                ///< 0x21-0x27 (7b): padding.
        u64  id;                ///< 0x28-0x2F (8b): unique job identifier.
        u64  batch_id;          ///< 0x30-0x37 (8b): batch id (0 = single job).
        bool tracked;           ///< 0x38: true = has associated task object.
                                ///< 0x39-0x3B (3b): padding.
        std::atomic<i32>
            unfinished_jobs;  ///< 0x3C-0x3F (4b): dependency counter.

        std::atomic<job_lifecycle> state;  ///< 0x40-0x43 (4b): lifecycle state.
                                           ///< 0x44-0x47: padding.

        job_t* parent;  ///< 0x48-0x4F (8b): parent job (for dependencies).

        task_state* task_state_ptr;  ///< 0x50-0x57 (8b): shared state with
                                     ///< task object. 0x58-0x7F: padding.
    };
    static_assert(sizeof(job_t) == 128, "job_t size must be 128 bytes");

    /// @brief per-worker context for job system. each worker thread has one of
    /// these to manage its local queue and state.
    struct worker_context {
        work_stealing_deque<job_t*>
                    _local_queue;  ///< per-worker work-stealing deque.
        std::thread _thread;       ///< worker thread handle.
        u64         index;         ///< worker index (0..n-1).

        /// @brief constructor. constructs a worker context with empty local
        /// queue.
        worker_context()
            : _local_queue(1024UL) {}  // todo: make this a configurable limit.
    };

    // --- constants ---
    // —————————————————————————————————————————————————————————————————————————

    // todo: make this configurable.
    static constexpr u64 GLOBAL_QUEUE_CAPACITY =
        4096;  ///< capacity of global job queues.

    // --- member variables ---
    // —————————————————————————————————————————————————————————————————————————

    // todo: atomic?
    bool _initialized = false;  ///< true if initialized and ready to use.

    // worker threads.
    std::vector<worker_context*> _workers;
    std::atomic<bool>            _shutdown_requested {false};

    // global job queues for high/normal/low priority jobs.
    mpmc_queue<job_t*> _global_high_queue {GLOBAL_QUEUE_CAPACITY};
    mpmc_queue<job_t*> _global_normal_queue {GLOBAL_QUEUE_CAPACITY};
    mpmc_queue<job_t*> _global_low_queue {GLOBAL_QUEUE_CAPACITY};

    // sleep / wake management for idle workers.
    mutable std::mutex      _sleep_mutex;
    std::condition_variable _sleep_cv;
    std::atomic<u64>        _sleeping_workers {0};

    // job id generation.
    std::atomic<u64> _next_job_id {1};
    std::atomic<u64> _next_batch_id {1};

    // pending job counter for drain().
    std::atomic<u64> _pending_count {0};

    // batch completion tracking.
    mutable std::mutex                         _completion_mutex;
    std::condition_variable                    _completion_cv;
    std::unordered_map<u64, std::atomic<i64>*> _batch_counters;

    // task state management.
    mutable std::mutex _state_mutex;  ///< protects state wait operations.

    // thread-local storage for worker identification.
    static thread_local u64  _tls_worker_index;
    static thread_local bool _tls_is_worker;

    // --- internal job management ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief allocate a new job from the memory system.
    auto allocate_job() -> job_t*;

    /// @brief free a job back to the memory system.
    auto free_job(job_t* j) -> void;

    /// @brief submit a job to the appropriate queue.
    auto submit_job(job_t* j) -> void;

    /// @brief mark a job as finished (handles state transitions and cleanup).
    auto finish_job(job_t* j) -> void;

    /// @brief handle batch completion bookkeeping.
    auto handle_batch_completion(job_t* j) -> void;

    // --- worker thread operations ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief main worker thread loop (blocking).
    auto worker_main(u64 worker_index) -> void;

    /// @brief get a job for execution (tries local, then steal, then global).
    auto get_job(u64 worker_index) -> job_t*;

    /// @brief try to steal a job from another worker's deque.
    auto try_steal(u64 worker_index) -> job_t*;

    /// @brief execute a single job and call finish_job.
    auto execute_job(job_t* j, u64 worker_index) -> void;

    /// @brief help execute jobs while waiting (for non-workers).
    auto help_with_work_external() -> void;

    // --- synchronization ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief wake up sleeping workers to process new jobs.
    auto wake_workers() -> void;

    /// @brief get the worker index for the current thread.
    auto get_current_worker_index() const -> u64;

    /// @brief check if current thread is a worker.
    auto is_worker_thread() const -> bool;

public:
    // --- ic_job ---
    // —————————————————————————————————————————————————————————————————————————

    auto fire(job_fn job_func, job_priority priority = job_priority::normal)
        -> void override;
    auto fire_batch(const job_fn* job_funcs,
                    u64           count,
                    job_priority  priority = job_priority::normal)
        -> void override;

    auto submit_with_state(task_state*  state,
                           job_fn       job_func,
                           job_priority priority) -> void override;

    auto drain() -> void override;

    auto wait_for_state(task_state* state) -> void override;
    auto release_state(task_state* state) -> void override;

    auto parallel_for(u64 begin, u64 end, parallel_fn func, u64 chunk_size = 0)
        -> void override;

    [[nodiscard]]
    auto is_fallback() const -> bool override {
        return false;  // real job system, not fallback.
    }

protected:
    auto submit_internal(std::function<void(void*)> wrapper,
                         usize                      result_size,
                         void (*result_deleter)(void*),
                         job_priority priority) -> task override;

private:
    // --- lifecycle ---
    // —————————————————————————————————————————————————————————————————————————

    /// @brief initialize the job system.
    /// @return true on success, false on failure.
    auto initialize() -> bool;

    /// @brief shutdown the job system.
    auto shutdown() -> void;
};

}  // namespace vent