#pragma once
//
// vent public sdk.
// mpmc - queue.
// ——————————————————————
// lock-free bounded multiple-producer multiple-consumer queue.
//
// todo: add profiling for debug builds to track potential contention/hotspots.
// where & what?
//  - align each cell to one cache line. depending on size of T, this could
//  require
//    significant memory overhead, but would reduce false sharing between
//    producers.
//  - exponential backoff on failed compare_exchange_weak in try_push/try_pop.

// todo: move this to header / source pair.

#include <_vent/vent_sdk.hpp>

#include <atomic>
#include <bit>      // for std::bit_ceil
#include <cstddef>  // for std::ptrdiff_t
#include <optional>
#include <utility>  // for std::move

#if defined(_MSC_VER)
    #include <intrin.h>
    #define VENT_PREFETCH(addr, rw, loc) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#else
    #define VENT_PREFETCH(addr, rw, loc) __builtin_prefetch(addr, rw, loc)
#endif

namespace vent {

/// @brief lock-free bounded mpmc queue.
/// @tparam T value type. must be movable.
template <typename T>
class mpmc_queue {
public:
    VENT_NO_COPY_MOVE(mpmc_queue)
    
private:
    // each data entry has a sequence number that acts like a tiny state
    // machine.
    //  producer expects seq == pos
    //  consumer expects seq == pos + 1
    // todo: check for potential performance problems. padding to CACHE_LINE
    // could be added to todo: reduce false sharing between cells.
    struct cell {
        std::atomic<u64> _seq;   ///< sequence number (state + ordering).
        T                _data;  ///< stored data.
    };

    u64   _capacity;  ///< number of cells. must be power of 2.
    u64   _mask;      ///< capacity - 1. used for fast modulo.
    cell* _buffer;    ///< ring buffer storage.

    // note: keep producer/consumer cursors on different cache lines.
    alignas(CACHE_LINE) std::atomic<u64> _enqueue_pos {
        0};  ///< producer cursor.
    alignas(CACHE_LINE) std::atomic<u64> _dequeue_pos {
        0};  ///< consumer cursor.
public:
    /// @brief create a queue with given capacity.
    /// @param capacity requested capacity. will be rounded up to power of 2.
    explicit mpmc_queue(u64 capacity = 1024)
        : _capacity(capacity),
          _mask(0),
          _buffer(nullptr) {
        if (_capacity < 2) {
            // todo: warn about invalid capacity. we hard-clamp to 2.
            _capacity = 2;
        }

        // ensure capacity is power of 2.
        if ((_capacity & (_capacity - 1)) != 0) {
            // todo: warn about capacity not being power of 2 & bigger size
            // being used.
            _capacity = std::bit_ceil(_capacity);
        }
        _mask = _capacity - 1;

        // todo: custom allocator with memory system & memory tracking.
        _buffer = new cell[_capacity];

        // initialize sequence numbers.
        // note: seq is used for correctness; _data is left default constructed.
        for (u64 i = 0; i < _capacity; ++i) {
            _buffer[i]._seq.store(i, std::memory_order_relaxed);
            _buffer[i]._data = T {};
        }
    }

    ~mpmc_queue() {
        delete[] _buffer;
        _buffer = nullptr;
    }

    /// @brief try to push an item into the queue.
    /// @param item item to enqueue.
    /// @return true if enqueued, false if queue was full.
    auto try_push(T item) -> bool {
        u64 pos = _enqueue_pos.load(std::memory_order_relaxed);
        for (;;) {
            cell* c = &_buffer[pos & _mask];

            // hint to compiler: we intend to write this location soon.
            // locality 3: try to keep this cached. we will write it now and
            // assume it will be used in some time, so do not remove it from
            // cache too eagerly.
            VENT_PREFETCH(c, 1, 3);
            u64 seq = c->_seq.load(std::memory_order_acquire);

            // dif == 0: slot is ready for this producer position.
            // dif < 0 : queue is full (producer is "too far ahead").
            // dif > 0 : another producer advanced; reload pos.
            std::ptrdiff_t dif = static_cast<std::ptrdiff_t>(seq) -
                                 static_cast<std::ptrdiff_t>(pos);

            if (dif == 0) {
                if (_enqueue_pos.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    // we own this slot now.
                    c->_data = std::move(item);
                    c->_seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                continue;
            }

            if (dif < 0) {
                // queue appears full.
                return false;
            }

            pos = _enqueue_pos.load(std::memory_order_relaxed);
        }
    }

    /// @brief try to pop an item from the queue.
    /// @return item if available, std::nullopt if queue was empty.
    auto try_pop() -> std::optional<T> {
        u64 pos = _dequeue_pos.load(std::memory_order_relaxed);
        for (;;) {
            cell* c   = &_buffer[pos & _mask];
            u64   seq = c->_seq.load(std::memory_order_acquire);

            // consumer expects seq == pos + 1.
            std::ptrdiff_t dif = static_cast<std::ptrdiff_t>(seq) -
                                 static_cast<std::ptrdiff_t>(pos + 1);

            if (dif == 0) {
                if (_dequeue_pos.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    // we own this slot now.
                    T item = std::move(c->_data);

                    // mark slot as free for the next wrap-around producer.
                    c->_seq.store(pos + _capacity, std::memory_order_release);
                    return item;
                }
                continue;
            }

            if (dif < 0) {
                // queue appears empty.
                return std::nullopt;
            }

            pos = _dequeue_pos.load(std::memory_order_relaxed);
        }
    }

    /// @brief get capacity of the queue.
    /// @return number of slots.
    [[nodiscard]]
    auto capacity() const -> u64 {
        return _capacity;
    }

    /// @brief approximate size (may be stale under contention).
    /// @return estimated number of items in queue.
    [[nodiscard]]
    auto size_approx() const -> u64 {
        u64 enq = _enqueue_pos.load(std::memory_order_relaxed);
        u64 deq = _dequeue_pos.load(std::memory_order_relaxed);
        return (enq >= deq) ? (enq - deq) : 0;
    }

    /// @brief check if queue is empty (may be stale under contention).
    [[nodiscard]]
    auto empty_approx() const -> bool {
        return size_approx() == 0;
    }
};

}  // namespace vent
