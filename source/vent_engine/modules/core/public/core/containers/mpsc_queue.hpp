#pragma once
//
// vent public sdk.
// mpsc-queue.
// ——————————————————————
// lock-free multiple-producer single-consumer queue.
// uses a ring buffer as data storage container with per-slod ready flags to
// synchronize producers and consumer.
//
// todo: add profiling for debug builds to track potential contention/hotspots.
// where & what?
//  - align each ready-flag to one cache line. this would take 64 bytes per slot
//    instead of 1 byte, but would reduce false sharing between producers. may
//    be used for high worker counts & high contention scenarios.
//  - exponential backoff on failed compare_exchange_weak in push
//  - count number of spin-waits in pop. can also be adaptive: 100 spins with
//  _mm_pause
//    (low latency) -> another 500 spins with std::this_thread::yield() (medium
//    latency)
//    -> sleep for 1ms (high latency). brings ROBUSTNESS.
//  - batch push & pop (if needed).

#include <_vent/vent_sdk.hpp>

#include <core/containers/circular_array.hpp>

#include <atomic>
#include <immintrin.h>  // for _mm_pause
#include <optional>     // for std::optional

#if defined(_MSC_VER)
    #include <intrin.h>
    #define VENT_PREFETCH(addr, rw, loc) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#else
    #define VENT_PREFETCH(addr, rw, loc) __builtin_prefetch(addr, rw, loc)
#endif

namespace vent {

/// @brief lock-free mpsc queue.
/// @tparam T value type. must be movable.
template <typename T>
class mpsc_queue {
public:
    VENT_NO_COPY_MOVE(mpsc_queue)
private:
    // note: head is written by all producers, it has a high contention
    // potential. note: tail is only written by the single consumer. no
    // contention there.
    alignas(CACHE_LINE) std::atomic<u64> _head {0};  ///< producer cursor.
    alignas(CACHE_LINE) std::atomic<u64> _tail {0};  ///< consumer cursor.

    circular_array<T> _data;         ///< ring buffer storage.
    std::atomic<u8>*  _ready_flags;  ///< per-slot ready flags.
public:
    /// @brief create a queue with given capacity.
    /// @param capacity requested capacity. will be rounded up to power of 2.
    explicit mpsc_queue(u64 capacity = 1024)
        : _data(capacity),
          _ready_flags(nullptr) {

        // capacity can be any value, not necessarily power of 2.
        // circular_array automatically rounds up to next power of 2.
        u64 real_cap = _data.capacity();
        _ready_flags = new std::atomic<u8>[real_cap];

        // initialize ready flags to 0 (not ready).
        for (u64 i = 0; i < real_cap; ++i)
            _ready_flags[i].store(0, std::memory_order_relaxed);
    }

    ~mpsc_queue() {
        delete[] _ready_flags;
        _ready_flags = nullptr;
    }

    /// @brief push an item into the queue. called by any producer thread.
    /// @param item item to enqueue.
    /// @return true if enqueued, false if queue was full or we raced against
    /// another producer and failed.
    auto push(T item) -> bool {
        // loop until we successfully reserve a slot or determine queue is full.
        while (true) {
            // load current head position.
            u64 slot = _head.load(std::memory_order_relaxed);
            u64 tail = _tail.load(std::memory_order_acquire);

            // check if queue is full.
            if (slot - tail >= _data.capacity())
                return false;  // queue is full.

            // try to reserve slot via CAS.
            // if CAS fails, another producer grabbed this slot - retry with
            // updated slot.
            if (_head.compare_exchange_weak(slot,
                                            slot + 1,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {

                // we own the slot 'slot'.
                u64 index = slot & (_data.capacity() - 1);
                VENT_PREFETCH(
                    &_ready_flags[index],
                    1,
                    1);  // hint the compiler we will write this soon.
                // write data to the circular array.
                _data.put(slot, std::move(item));

                // mark slot as ready for consumer.
                // release ensures data write is visible before flag.

                _ready_flags[index].store(1, std::memory_order_release);

                return true;
            }

            // CAS failed - another producer was faster.
            // loop continues with updated 'slot' value from
            // compare_exchange_weak.
        }
    }

    /// @brief pop an item from the queue. called by the single consumer thread.
    /// @return item if available, std::nullopt if queue was empty (or producer
    /// hasn't written yet).
    auto pop() -> std::optional<T> {
        u64 slot = _tail.load(std::memory_order_relaxed);
        u64 head = _head.load(std::memory_order_acquire);

        // check if queue is empty.
        if (slot >= head)
            return std::nullopt;

        u64 index = slot & (_data.capacity() - 1);

        // hint the compiler we will read these soon.
        VENT_PREFETCH(&_ready_flags[index], 0, 3);  // read
        VENT_PREFETCH(&_data[slot], 0, 3);          // read

        while (_ready_flags[index].load(std::memory_order_acquire) == 0) {
            // producer has not written data yet.
            // spin wait until producer has written data.

#if defined(__x86_64__) || defined(_M_X64)
            _mm_pause();  // x86 pause instruction.
#endif
        }

        // slot is ready. we can read data now.
        T item = _data.get(slot);

        // mark slot as not ready for next wrap-around producer.
        _ready_flags[index].store(0, std::memory_order_relaxed);
        _tail.store(slot + 1, std::memory_order_relaxed);

        return item;
    }

    /// @brief try to pop an item from the queue. called by the single consumer
    /// thread. no spin-waiting.
    /// @return item if available, std::nullopt if queue was empty or producer
    /// hasn't written yet.
    /// @note this is a non-blocking version of pop().
    auto try_pop() -> std::optional<T> {
        u64 slot = _tail.load(std::memory_order_relaxed);
        u64 head = _head.load(std::memory_order_acquire);

        if (slot >= head) {
            return std::nullopt;  // empty.
        }

        u64 index = slot & (_data.capacity() - 1);

        // check if slot is ready (non-blocking).
        if (_ready_flags[index].load(std::memory_order_acquire) == 0) {
            return std::nullopt;  // producer hasn't written yet.
        }

        T item = _data.get(slot);
        _ready_flags[index].store(0, std::memory_order_relaxed);
        _tail.store(slot + 1, std::memory_order_relaxed);

        return item;
    }

    /// @brief get approximate size.
    /// @return estimated number of items (may be stale).
    auto size() const -> u64 {
        u64 head = _head.load(std::memory_order_relaxed);
        u64 tail = _tail.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    /// @brief check if empty.
    auto empty() const -> bool { return size() == 0; }

    /// @brief get capacity.
    auto capacity() const -> u64 { return _data.capacity(); }
};

}  // namespace vent

/*


principles of mpsc queue:
- ring buffer storage:
    - fixed-size circular array.
    - producers write to head position, consumer reads from tail position.
- multiple producers can push entries concurrently. -> CAS needed on head.
- single consumer pops entries. -> no CAS needed on tail.

CAS = compare_exchange_weak(expected, desired, success_memorder,
failure_memorder).
      -> compare-and-swap atomic operation. atomically compares a value to an
expected value and, if they match, swaps it with a new value. returns whether
the swap was successful.

data storage:
1. producer loads head and tail positions (current read and write cursors).
2. producer checks if queue is full: (head - tail) >= capacity -> full (circular
buffer wrap-around).
3. producer does CAS(head, head+1) [compare current head with loaded head, if
they match set head to head+1].
    - if CAS fails, another producer got inbetween - retry from step 1 with
updated head.
4. on CAS success, producer owns slot 'head'.
5. producer writes data to circular array at index 'head'.
6. producer marks slot 'head' as ready (using a per-slot ready flag).

data retrieval:
1. consumer loads tail and head positions. no contention needed, because only
one thread ever writes tail.
2. consumer checks if queue is empty: tail >= head -> empty.
3. consumer checks if slot 'tail' is ready (using per-slot ready flag).
    - if not ready, spin-wait until producer marks it as ready.
4. on ready, consumer reads data from circular array at index 'tail'.
5. consumer marks slot 'tail' as not ready (for next wrap-around producer).






*/