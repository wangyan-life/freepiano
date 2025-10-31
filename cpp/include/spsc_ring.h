#pragma once
// Simple high-quality single-producer single-consumer ring buffer (header-only)
// - power-of-two capacity
// - contiguous memory
// - lock-free using std::atomic
// - minimal ABI: template class usable from C++ components

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <memory>
#include <type_traits>
#if defined(_WIN32)
#include <malloc.h> // _aligned_malloc/_aligned_free
#endif

namespace fp {

template<typename T>
class SpscRing {
    static_assert(std::is_trivially_copyable<T>::value, "SpscRing requires trivially copyable types");
public:
    explicit SpscRing(size_t capacity_pow2)
    {
        assert(capacity_pow2 >= 2 && (capacity_pow2 & (capacity_pow2 - 1)) == 0);
        capacity = capacity_pow2;
        mask = capacity - 1;
    // allocate 64-byte aligned buffer to avoid false-sharing and satisfy alignas(64)
    const size_t bytes = sizeof(T) * capacity;
#if defined(_WIN32)
    buffer = static_cast<T*>(_aligned_malloc(bytes, 64));
    assert(buffer != nullptr && "_aligned_malloc failed");
#else
    // POSIX aligned allocation (C11) if available
    void* p = nullptr;
#  if defined(_ISOC11_SOURCE)
    p = aligned_alloc(64, bytes);
#  else
    if (posix_memalign(&p, 64, bytes) != 0) p = nullptr;
#  endif
    buffer = static_cast<T*>(p);
    assert(buffer != nullptr && "aligned allocation failed");
#endif
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    ~SpscRing()
    {
#if defined(_WIN32)
    _aligned_free(buffer);
#else
    free(buffer);
#endif
    }

    // non-copyable
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // capacity in elements
    size_t size() const noexcept { return capacity; }

    // returns number of elements available for pop
    size_t available() const noexcept
    {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        return h - t;
    }

    // returns free space for push
    size_t free_space() const noexcept
    {
        return capacity - available() - 1; // keep one slot empty to distinguish full/empty
    }

    // push up to 'count' items from src, returns number actually pushed
    size_t push(const T* src, size_t count) noexcept
    {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        size_t free = capacity - (h - t) - 1;
        if (count > free) count = free;
        size_t first = std::min(count, capacity - (h & mask));
        for (size_t i = 0; i < first; ++i) {
            buffer[(h + i) & mask] = src[i];
        }
        for (size_t i = first; i < count; ++i) {
            buffer[(h + i) & mask] = src[i];
        }
        head.store(h + count, std::memory_order_release);
        return count;
    }

    // pop up to 'count' items into dst, returns number actually popped
    size_t pop(T* dst, size_t count) noexcept
    {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        size_t available = h - t;
        if (count > available) count = available;
        size_t first = std::min(count, capacity - (t & mask));
        for (size_t i = 0; i < first; ++i) {
            dst[i] = buffer[(t + i) & mask];
        }
        for (size_t i = first; i < count; ++i) {
            dst[i] = buffer[(t + i) & mask];
        }
        tail.store(t + count, std::memory_order_release);
        return count;
    }

private:
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
    size_t capacity;
    size_t mask;
    T* buffer;
};

} // namespace fp
