#pragma once
//
// job module.
// work-stealing container.
// ——————————————————————
//
// lock-free queue with SPMC (single-producer, multiple-consumer) semantics.
//
// owning thread » push/pop to the bottom of the deque.
// other threads » steal from the top of the deque.
// in DEBUG mode, checks are performed to ensure only the owner thread calls
//   push/pop methods.

#include <_vent/vent_sdk.hpp>

#include <core/containers/circular_array.hpp>

// additional safety checks in debug builds. we'll blindly assume other builds
// behave exactly like debug builds, so that we can check in debug but avoid
// the overhead in release/ship.
#ifdef VENT_DEBUG
    #include <thread>
#endif

#include <algorithm>
#include <atomic>
#include <optional>
#include <utility>
#include <memory>
#include <vector>

namespace vent {

template <typename T>
class work_stealing_deque {
private:
    /// @brief read & written by thieves, read by owner.
    alignas(CACHE_LINE) std::atomic<u64> _top {0};

    /// @brief written by owner, read by thieves.
    alignas(CACHE_LINE) std::atomic<u64> _bottom {0};
    // note: we intend `_top` and `_bottom` to be on separate cache lines to
    // reduce false sharing.

    /// @brief the live circular storage, published as a raw pointer so thieves
    ///        can read it atomically. the owner swaps this on grow with a release
    ///        store; thieves acquire-load it in steal(). storing a raw pointer
    ///        (not the unique_ptr) is what makes the array switch a single atomic
    ///        publish rather than a racy multi-step reassignment.
    alignas(CACHE_LINE) std::atomic<circular_array<T>*> _array;

    /// @brief owns every array the deque has ever allocated (current + outgrown).
    ///        outgrown arrays are never freed while the deque lives, because a
    ///        thief may still hold a raw pointer into one it acquire-loaded just
    ///        before a grow. growth is geometric (doubling), so the total memory
    ///        retained is bounded to ~2x the final array — cheap, and it removes
    ///        any need for hazard pointers / epoch reclamation at this scale.
    ///        only the owner thread touches this vector (during push-grow), so it
    ///        needs no synchronization of its own.
    std::vector<std::unique_ptr<circular_array<T>>> _arrays;

#ifdef VENT_DEBUG
    std::atomic<std::thread::id> _owner_thread_id {std::thread::id()};
#endif

public:
    explicit work_stealing_deque(u32 capacity = 256) {
        // allocate the initial array, keep ownership in _arrays, and publish its
        // raw pointer. relaxed is fine: no thieves can observe us during
        // construction.
        auto initial = std::make_unique<circular_array<T>>(capacity);
        _array.store(initial.get(), std::memory_order_relaxed);
        _arrays.push_back(std::move(initial));
    }

    ~work_stealing_deque() = default;

#ifdef VENT_DEBUG
    /// set owner thread id from the actual worker thread (call at thread start).
    auto set_owner_thread_id(std::thread::id id) -> void {
        _owner_thread_id.store(id, std::memory_order_relaxed);
    }
#endif

    // no copy/move.
    work_stealing_deque(const work_stealing_deque&)            = delete;
    work_stealing_deque& operator=(const work_stealing_deque&) = delete;
    work_stealing_deque(work_stealing_deque&&)                 = delete;
    work_stealing_deque& operator=(work_stealing_deque&&)      = delete;

    /// @brief push an item onto the bottom of the deque.
    /// @param item item to push onto the deque.
    /// @warning this method is NOT thread-safe. only the owning thread may call
    /// push().
    auto push(T item) -> void {
#ifdef VENT_DEBUG
        /*if (_owner_thread_id.load(std::memory_order_relaxed) !=
            std::this_thread::get_id())
            log()->warn(
                "jobs",
                "work_stealing_deque::push() called from non-owner thread.");
        */
        // debug-only check: ensure only owner thread calls push().
        // note: can't use log() here since we're a template in a private header
        // without access to logging interfaces. this is acceptable since wrong-
        // thread access will cause undefined behavior that will surface in
        // testing. todo: consider a lightweight assert mechanism that doesn't
        // need interfaces.
        (void) _owner_thread_id;  // suppress unused warning if we later remove
                                  // check.
#endif

        u64 b = _bottom.load(std::memory_order_relaxed);
        u64 t = _top.load(std::memory_order_relaxed);

        // the owner is the only writer of _array, so a relaxed load always sees
        // our own latest publish.
        circular_array<T>* a = _array.load(std::memory_order_relaxed);

        // check if full.
        if (b - t >= a->capacity()) {
            // grow: allocate a bigger array and copy the live [t, b) range into
            // it. the old array stays alive in _arrays (a thief may still hold a
            // pointer into it), and we publish the new one with a single release
            // store — there is never a moment where _array is null or half-set,
            // so a concurrent steal() always reads a fully-valid array.
            auto               new_array = a->grow(t, b);
            circular_array<T>* new_raw   = new_array.get();
            _arrays.push_back(std::move(new_array));
            _array.store(new_raw, std::memory_order_release);
            a = new_raw;
        }
        // place item in array.
        a->put(b, std::move(item));
        // make the item visible to other threads.
        std::atomic_thread_fence(std::memory_order_release);
        _bottom.store(b + 1, std::memory_order_relaxed);
    }

    /// @brief pop an item from the bottom of the deque.
    /// @return item if successful, std::nullopt if deque was empty.
    /// @warning this method is NOT thread-safe. only the owning thread may call
    /// pop().
    auto pop() -> std::optional<T> {
#ifdef VENT_DEBUG
        /*if (_owner_thread_id.load(std::memory_order_relaxed) !=
            std::this_thread::get_id())
            log()->warn(
                "jobs",
                "work_stealing_deque::pop() called from non-owner thread.");
        */
        // debug-only check: ensure only owner thread calls push().
        // note: can't use log() here since we're a template in a private header
        // without access to logging interfaces. this is acceptable since wrong-
        // thread access will cause undefined behavior that will surface in
        // testing. todo: consider a lightweight assert mechanism that doesn't
        // need interfaces.
        (void) _owner_thread_id;  // suppress unused warning if we later remove
                                  // check.
#endif

        u64 bottom_val = _bottom.load(std::memory_order_relaxed);
        u64 top_val    = _top.load(std::memory_order_relaxed);

        // empty check.
        if (bottom_val <= top_val) {
            return std::nullopt;
        }

        u64                b = bottom_val - 1;
        // owner-only writer of _array: relaxed load sees our own latest publish.
        circular_array<T>* a = _array.load(std::memory_order_relaxed);
        _bottom.store(b, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        u64 t = _top.load(std::memory_order_relaxed);

        if (t <= b) [[likely]] {
            T item = a->get(b);
            if (t == b) [[unlikely]] {
                // last item, might race with thieves. if the cas fails a thief
                // took this exact item, so we own nothing: report empty via
                // nullopt rather than returning a default-constructed T{} (which
                // for job_t* is a null pointer the caller would treat as a real
                // job and dereference/skip a work-check on). either way bottom
                // is restored to b+1 to reflect the now-empty deque.
                if (!_top.compare_exchange_strong(t,
                                                  t + 1,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed)) {
                    _bottom.store(b + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                _bottom.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        }

        // queue empty. restore bottom.
        _bottom.store(b + 1, std::memory_order_relaxed);
        return std::nullopt;
    }

    /// @brief steal an item from the top of the deque. called by any thread.
    /// @return item if successful, std::nullopt if deque was empty or another
    /// thief beat us.
    auto steal() -> std::optional<T> {
        u64 t = _top.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        u64 b = _bottom.load(std::memory_order_acquire);

        if (t < b) [[likely]] {
            // acquire-load pairs with the release store in push()'s grow: it
            // guarantees we read a fully-constructed array (never a stale or null
            // pointer) and that the copied elements are visible.
            circular_array<T>* a    = _array.load(std::memory_order_acquire);
            T                  item = a->get(t);

            if (!_top.compare_exchange_strong(t,
                                              t + 1,
                                              std::memory_order_seq_cst,
                                              std::memory_order_relaxed)) {
                return std::nullopt;
            }
            return item;
        }
        return std::nullopt;
    }

    /// @brief get current size of the deque.
    auto size() const -> u64 {
        u64 b = _bottom.load(std::memory_order_relaxed);
        u64 t = _top.load(std::memory_order_relaxed);
        return (b > t) ? (b - t) : 0u;
    }

    /// @brief check if the deque is empty.
    auto empty() const -> bool { return size() == 0; }
};

}  // namespace vent
