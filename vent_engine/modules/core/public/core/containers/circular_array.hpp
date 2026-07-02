#pragma once
//
// vent public sdk.
// circular array container.
// ——————————————————————
//
// simple circular array container with power-of-2 capacity.
// not thread-safe. designed for use in lock-free data structures
// that provide their own synchronization (e.g., work-stealing deque).

#include <_vent/vent_sdk.hpp>

#include <algorithm>  // std::move
#include <bit>        // std::bit_ceil

namespace vent {

template <typename T>
class circular_array {
public:
    VENT_NO_COPY_MOVE(circular_array)
private:
    u64 _capacity;  ///< total slots in the array. MUST be power of 2.
    u64 _mask;      ///< fast-modulo mask for wrapping indices that are too big.
    T*  _data;      ///< pointer to the data array.
public:
    explicit circular_array(u64 capacity)
        : _capacity(capacity),
          _mask(capacity - 1),
          _data(nullptr) {

        // ensure capacity is power of 2.
        // if not, round up to next power of 2.
        if ((capacity & (capacity - 1)) != 0) {
            // todo: warn about capacity not being power of 2 & bigger size
            // being used.
            _capacity = std::bit_ceil(capacity);
            _mask     = _capacity - 1;
        }

        // allocate data array.
        // todo: custom allocator with memory system & memory tracking.
        // todo: allocate with alignment.
        _data = new T[_capacity];

        // initialize data to default values.
        for (u64 i = 0; i < _capacity; ++i) {
            _data[i] = T {};
        }
    }
    ~circular_array() { delete[] _data; }

    /// @brief get current capacity.
    /// @return capacity of the circular array.
    auto capacity() const -> u64 { return _capacity; }

    /// @brief get item at index (wraps around).
    /// @param index index to get item from.
    /// @return copy of item at index.
    auto get(u64 index) const -> T { return _data[index & _mask]; }

    /// @brief put item at index (wraps around).
    /// @param index index to put item at.
    /// @param item item to put at index.
    auto put(u64 index, T item) -> void {
        _data[index & _mask] =
            std::move(item);  // std::move avoids copying the object.
    }

    /// @brief create a new, larger circular array and copy elements in range
    /// [bottom, top].
    /// @param bottom bottom index (inclusive).
    /// @param top top index (exclusive).
    /// @return pointer to the new, larger circular array (caller is owning).
    auto grow(u64 bottom, u64 top) -> circular_array* {
        // create new array with double capacity.
        circular_array* new_array = new circular_array(_capacity * 2);

        // copy existing items to new array.
        for (u64 i = bottom; i < top; ++i)
            new_array->put(i, get(i));
        return new_array;
    }

    /// @brief array subscript operator for indexed access (wraps around).
    /// @param index index to access.
    /// @return reference to item at wrapped index.
    auto operator[](u64 index) -> T& { return _data[index & _mask]; }

    /// @brief const array subscript operator.
    /// @param index index to access.
    /// @return const reference to item at wrapped index.
    auto operator[](u64 index) const -> const T& {
        return _data[index & _mask];
    }
};

}  // namespace vent