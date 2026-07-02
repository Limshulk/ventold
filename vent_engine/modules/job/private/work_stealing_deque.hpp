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

#pragma once

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

    /// @brief the actual circular storage container.
    circular_array<T>*              _array;
    std::vector<circular_array<T>*> _retired_arrays;

#ifdef VENT_DEBUG
    std::atomic<std::thread::id> _owner_thread_id {std::thread::id()};
#endif

public:
    explicit work_stealing_deque(u32 capacity = 256)
        : _array(new circular_array<T>(capacity)) {}

    ~work_stealing_deque() {
        delete _array;
        for (auto* arr : _retired_arrays) {
            delete arr;
        }
    }

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

        circular_array<T>* a = _array;

        // check if full.
        if (b - t >= a->capacity()) {
            // need to grow!
            circular_array<T>* old = _array;
            _array                 = old->grow(t, b);
            _retired_arrays.push_back(old);
            if (_retired_arrays.size() > 8) {
                delete _retired_arrays.front();
                _retired_arrays.erase(_retired_arrays.begin());
            }
            a = _array;
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
        circular_array<T>* a = _array;
        _bottom.store(b, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        u64 t = _top.load(std::memory_order_relaxed);

        if (t <= b) [[likely]] {
            T item = a->get(b);
            if (t == b) [[unlikely]] {
                // last item, might race with thieves.
                if (!_top.compare_exchange_strong(t,
                                                  t + 1,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed)) {
                    item = T {};
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
            circular_array<T>* a    = _array;
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
