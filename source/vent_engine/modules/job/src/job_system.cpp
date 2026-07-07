// job module.
// job_system implementation.
// ——————————————————————
//
// todo: write description.

// todo: requires heavy refactoring and commenting.

#include <job_system.hpp>

#include <core/thread_registry.hpp>
#include <core/system/registration.hpp>

#include <_vent/accessors.hpp>

#include <algorithm>
#include <cstdlib>
#include <random>

#ifdef __linux__
    #include <pthread.h>
#endif

namespace vent {

// define thread-local identifiers declared in header.
thread_local u64  job_system::_tls_worker_index = SIZE_MAX;
thread_local bool job_system::_tls_is_worker    = false;

job_system::~job_system() {
    if (_initialized) {
        shutdown();
    }
}

auto job_system::initialize() -> bool {
    if (_initialized) {
        // todo: log.
        return true;
    }

    // todo: implement config.
    // awefull casting happens here, i am so sorry!
    // basically, we want to use auto-detection if worker count is <= 0.
    // negative values mean reserve some threads from total.
    i64 required_worker_count = 0;  // _config.worker_count;
    if (required_worker_count <= 0) {
        u64 total_cores       = std::thread::hardware_concurrency();
        required_worker_count = static_cast<i64>(
            (total_cores > static_cast<u64>(abs(required_worker_count)))
                ? total_cores + required_worker_count
                : 1);
    }
    u64 worker_count = static_cast<u64>(required_worker_count);

    // capture the main thread id. job is a bootstrap system, so initialize()
    // runs on the main thread — its id is exactly the thread that will later
    // drain the affinity inbox via run_pinned_jobs().
    _main_thread_id = std::this_thread::get_id();

    log()->trace(
        "job_system", "initializing job system with {} workers.", worker_count);

    _shutdown_requested = false;
    _pending_count      = 0;

    _workers.reserve(worker_count);
    for (u64 i = 0; i < worker_count; ++i) {
        auto* ctx  = new worker_context();
        ctx->index = i;
        _workers.push_back(ctx);
    }

    for (u64 i = 0; i < worker_count; ++i) {
        std::string name     = std::format("W:{:02}", i);
        _workers[i]->_thread = thread_registry::spawn_thread(name, [this, i]() {
            worker_main(i);
        });
    }

    _initialized = true;

    return true;
}

auto job_system::shutdown() -> void {
    log()->trace("job_system", "job_system::shutdown() called.");
    if (!_initialized) {
        return;
    }

    _initialized = false;

    // drain all pending jobs before full shutdown.
    drain();

    // signal workers to exit.
    _shutdown_requested = true;
    wake_workers();

    // join worker threads.
    for (auto* ctx : _workers) {
        if (ctx->_thread.joinable()) {
            ctx->_thread.join();
        }
    }

    // clear all remaining queues. these jobs will never be executed.
    while (auto j = _global_high_queue.try_pop()) {
        free_job(*j);
    }
    while (auto j = _global_normal_queue.try_pop()) {
        free_job(*j);
    }
    while (auto j = _global_low_queue.try_pop()) {
        free_job(*j);
    }
    while (auto j = _main_affinity_queue.try_pop()) {
        free_job(*j);
    }

    {
        std::lock_guard lock(_completion_mutex);
        for (auto& [id, counter] : _batch_counters) {
            delete counter;
        }
        _batch_counters.clear();
    }

    for (auto* ctx : _workers) {
        delete ctx;
    }
    _workers.clear();

    log()->trace("job_system", "job_system::shutdown() complete");
}

auto job_system::fire(job_fn       job_func,
                      job_priority priority,
                      job_affinity affinity) -> void {
    if (!_initialized || !job_func)
        return;

    job_t* j          = allocate_job();
    j->func           = std::move(job_func);
    j->priority       = priority;
    j->affinity       = affinity;
    j->id             = _next_job_id.fetch_add(1, std::memory_order_relaxed);
    j->batch_id       = 0;
    j->tracked        = false;
    j->task_state_ptr = nullptr;
    j->unfinished_jobs.store(1, std::memory_order_relaxed);
    j->state.store(job_lifecycle::pending, std::memory_order_release);
    j->parent = nullptr;

    _pending_count.fetch_add(1, std::memory_order_relaxed);
    submit_job(j);
}

auto job_system::fire_batch(const job_fn* jobs,
                            u64           count,
                            job_priority  priority) -> void {
    if (!_initialized || !jobs || count == 0)
        return;

    for (u64 i = 0; i < count; ++i) {
        if (jobs[i])
            fire(jobs[i], priority);
    }
}

auto job_system::submit_with_state(task_state*  state,
                                   job_fn       job_func,
                                   job_priority priority,
                                   job_affinity affinity) -> void {
    if (!_initialized || !state || !job_func) {
        return;
    }

    job_t* j          = allocate_job();
    j->func           = std::move(job_func);
    j->priority       = priority;
    j->affinity       = affinity;
    j->id             = _next_job_id.fetch_add(1, std::memory_order_relaxed);
    j->batch_id       = 0;
    j->tracked        = true;
    j->task_state_ptr = state;
    j->unfinished_jobs.store(1, std::memory_order_relaxed);
    j->state.store(job_lifecycle::pending, std::memory_order_release);
    j->parent = nullptr;

    state->id      = j->id;
    state->job_ptr = j;

    _pending_count.fetch_add(1, std::memory_order_relaxed);
    submit_job(j);
}

auto job_system::submit_internal(std::function<void(void*)> wrapper,
                                 usize                      result_size,
                                 void (*result_deleter)(void*),
                                 job_priority               priority,
                                 job_affinity               affinity) -> task {
    // allocate task state (shared between job and task handle).
    auto* state = new task_state();

    // allocate result storage if needed.
    if (result_size > 0) {
        state->result_ptr     = std::malloc(result_size);
        state->result_size    = result_size;
        state->result_deleter = result_deleter;
    }

    // wrap the callable to execute and store result with exception handling.
    job_fn job_func = [wrapper = std::move(wrapper), state]() mutable {
        try {
            wrapper(state->result_ptr);
        } catch (...) {
            state->exception = std::current_exception();
        }
    };

    // submit the job.
    submit_with_state(state, std::move(job_func), priority, affinity);

    return make_task(state);
}

auto job_system::wait_for_state(task_state* state) -> void {
    if (!state) {
        return;
    }

    if (state->completed.load(std::memory_order_acquire)) {
        return;
    }

    bool is_batch = (state->id & (1ULL << 63)) != 0;

    if (is_batch) {
        u64 batch_id = state->id & ~(1ULL << 63);

        while (true) {
            {
                std::lock_guard lock(_completion_mutex);
                auto            it = _batch_counters.find(batch_id);
                if (it == _batch_counters.end() || it->second->load() == 0) {
                    state->completed.store(true, std::memory_order_release);
                    return;
                }
            }

            help_with_work_external();
        }
    } else {
        while (!state->completed.load(std::memory_order_acquire)) {
            help_with_work_external();
        }
    }
}

auto job_system::release_state(task_state* state) -> void {
    if (!state) {
        return;
    }

    u32 prev = state->ref_count.fetch_sub(1, std::memory_order_acq_rel);

    if (prev == 1) {
        delete state;
    }
}

auto job_system::drain() -> void {
    log()->trace("job_system",
                 "drain() called. pending jobs: {}",
                 _pending_count.load());
    while (_pending_count.load(std::memory_order_relaxed) > 0) {
        // also pump the main inbox: if we are the main thread (e.g. shutdown
        // drain), main-pinned jobs would otherwise never run and this would spin
        // forever. run_pinned_jobs() is a no-op off the main thread.
        run_pinned_jobs();
        help_with_work_external();
    }
}

auto job_system::run_pinned_jobs() -> void {
    // only the main thread owns an inbox today. off-thread this is a no-op, which
    // is why it is safe to call unconditionally (e.g. from drain()).
    if (std::this_thread::get_id() != _main_thread_id) {
        return;
    }

    // drain everything queued at entry. jobs pinned during this drain (a pinned
    // job that itself pins more work) simply get picked up next frame — we do not
    // loop forever chasing a moving tail.
    while (auto j = _main_affinity_queue.try_pop()) {
        execute_job(*j, SIZE_MAX);
    }
}

auto job_system::parallel_for(u64         begin,
                              u64         end,
                              parallel_fn func,
                              u64         chunk_size) -> void {
    if (begin >= end || !func) {
        return;
    }

    u64 count = end - begin;

    // auto-detect chunk size if not specified.
    // aim for roughly one chunk per worker thread.
    if (chunk_size == 0) {
        u64 num_workers = std::max<u64>(1, _workers.size());
        chunk_size = std::max<u64>(1, (count + num_workers - 1) / num_workers);
    }

    u64 num_chunks = (count + chunk_size - 1) / chunk_size;

    std::vector<job_fn> job_funcs;
    job_funcs.reserve(num_chunks);

    for (u64 i = begin; i < end; i += chunk_size) {
        u64 chunk_end = (std::min) (i + chunk_size, end);
        job_funcs.push_back([func, i, chunk_end]() {
            for (u64 j = i; j < chunk_end; ++j) {
                func(j);
            }
        });
    }

    std::vector<task> tasks;
    tasks.reserve(job_funcs.size());

    for (auto& job_func : job_funcs) {
        tasks.push_back(submit(std::move(job_func), job_priority::normal));
    }

    for (auto& t : tasks) {
        t.wait();
    }
}

auto job_system::allocate_job() -> job_t* {
    return new job_t();
}

auto job_system::free_job(job_t* j) -> void {
    if (!j)
        return;
    delete j;
}

auto job_system::submit_job(job_t* j) -> void {
    // affinity routing takes precedence over everything else: a pinned job must
    // land in its target thread's inbox, never a worker deque or global queue.
    if (j->affinity == job_affinity::main) {
        if (_main_affinity_queue.try_push(j)) {
            // the main thread will run it at the next frame-start drain. no wake
            // needed — main polls its inbox every frame rather than sleeping.
            return;
        }
        // inbox full (pathological: > a full frame of pinned work outstanding).
        // if we ARE the main thread, run it inline now — safe, and it avoids a
        // self-deadlock where main spins pushing while being the only drainer.
        if (std::this_thread::get_id() == _main_thread_id) {
            execute_job(j, SIZE_MAX);
            return;
        }
        // otherwise spin until main drains space. running inline here would break
        // the affinity guarantee (wrong thread), which is the whole point.
        while (!_main_affinity_queue.try_push(j)) {
            std::this_thread::yield();
        }
        return;
    }

    if (is_worker_thread()) {
        u64 idx = get_current_worker_index();
        _workers[idx]->_local_queue.push(j);
        wake_workers();
        return;
    }

    constexpr auto MAX_RETRIES =
        5;  // todo: limit::jobs::max_push_retries; // configurable.
    bool pushed = false;

    for (u32 retry = 0; retry < MAX_RETRIES && !pushed; ++retry) {
        switch (j->priority) {
            case job_priority::high:
                pushed = _global_high_queue.try_push(j);
                break;
            case job_priority::low:
                pushed = _global_low_queue.try_push(j);
                break;
            default: pushed = _global_normal_queue.try_push(j); break;
        }

        if (!pushed && retry < MAX_RETRIES - 1) {
            std::this_thread::sleep_for(std::chrono::microseconds(
                /*queue_push_config::base_backoff_us*/ 1 << retry));
        }
    }

    if (pushed) {
        wake_workers();
    } else {
        // log()->warn(
        //     "jobs", "global queue full, executing job inline (id={}).", j->id);
        execute_job(j, SIZE_MAX);
    }
}

auto job_system::finish_job(job_t* j) -> void {
    i32 remaining =
        j->unfinished_jobs.fetch_sub(1, std::memory_order_acq_rel) - 1;

    if (remaining == 0) {
        if (j->parent) {
            finish_job(j->parent);
        }

        _pending_count.fetch_sub(1, std::memory_order_relaxed);

        if (j->batch_id != 0) {
            handle_batch_completion(j);
            free_job(j);
        } else if (!j->tracked) {
            j->state.store(job_lifecycle::done, std::memory_order_release);
            free_job(j);
        } else {
            task_state* state = j->task_state_ptr;

            if (state) {
                state->completed.store(true, std::memory_order_release);
            }

            free_job(j);

            if (state) {
                release_state(state);
            }
        }
    }
}

auto job_system::handle_batch_completion(job_t* j) -> void {
    task_state* state = j->task_state_ptr;

    std::lock_guard lock(_completion_mutex);
    auto            it = _batch_counters.find(j->batch_id);
    if (it == _batch_counters.end()) {
        // log()->error(
        //     "jobs", "batch counter not found for batch_id={}.", j->batch_id);
        return;
    }
    if (it->second->fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (state) {
            state->completed.store(true, std::memory_order_release);
            release_state(state);
        }

        delete it->second;
        _batch_counters.erase(it);
        _completion_cv.notify_all();
    }
}

auto job_system::worker_main(u64 worker_index) -> void {
    _tls_worker_index = worker_index;
    _tls_is_worker    = true;

#ifdef VENT_DEBUG
    _workers[worker_index]->_local_queue.set_owner_thread_id(
        std::this_thread::get_id());
#endif

    log()->trace("job_system", "worker {} started.", worker_index);

    while (!_shutdown_requested.load(std::memory_order_relaxed)) {
        job_t* j = get_job(worker_index);

        if (j) {
            execute_job(j, worker_index);
        } else {
            std::unique_lock lock(_sleep_mutex);
            _sleeping_workers.fetch_add(1, std::memory_order_relaxed);

            if (!_shutdown_requested.load(std::memory_order_relaxed))
                _sleep_cv.wait_for(lock,
                                   std::chrono::milliseconds(
                                       /*worker_config::idle_sleep_ms*/ 1));

            _sleeping_workers.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    log()->trace("job_system", "worker {} stopped.", worker_index);
}

auto job_system::get_job(u64 worker_index) -> job_t* {
    if (auto j = _workers[worker_index]->_local_queue.pop()) {
        return *j;
    }

    if (auto j = _global_high_queue.try_pop()) {
        return *j;
    }
    if (auto j = _global_normal_queue.try_pop()) {
        return *j;
    }
    if (auto j = _global_low_queue.try_pop()) {
        return *j;
    }

    return try_steal(worker_index);
}

auto job_system::try_steal(u64 thief_index) -> job_t* {
    u64 num_workers = _workers.size();
    if (num_workers <= 1) {
        return nullptr;
    }

    static thread_local std::mt19937 rng(std::random_device {}());
    u64                              start = rng() % num_workers;

    for (u64 i = 0; i < num_workers; ++i) {
        u64 target = (start + i) % num_workers;
        if (target == thief_index) {
            continue;
        }

        auto* target_queue = &_workers[target]->_local_queue;
        if (target_queue->empty()) {
            continue;
        }

        if (auto j = target_queue->steal()) {
            return *j;
        }
    }

    return nullptr;
}

auto job_system::execute_job(job_t* j, u64 /*worker_index*/) -> void {
    if (j && j->func) {
        j->func();
    }

    finish_job(j);
}

auto job_system::help_with_work_external() -> void {
    if (auto j = _global_high_queue.try_pop()) {
        execute_job(*j, SIZE_MAX);
    } else if (auto j = _global_normal_queue.try_pop()) {
        execute_job(*j, SIZE_MAX);
    } else if (auto j = _global_low_queue.try_pop()) {
        execute_job(*j, SIZE_MAX);
    } else {
        std::this_thread::yield();
    }
}

auto job_system::wake_workers() -> void {
    if (_sleeping_workers.load(std::memory_order_relaxed) > 0) {
        _sleep_cv.notify_one();
    }
}

auto job_system::get_current_worker_index() const -> u64 {
    return _tls_worker_index;
}

auto job_system::is_worker_thread() const -> bool {
    return _tls_is_worker;
}

}  // namespace vent

// register the job system for automatic discovery.
VENT_REGISTER_SYSTEM(vent::job_system, vent::ic_job, vent::i_job)