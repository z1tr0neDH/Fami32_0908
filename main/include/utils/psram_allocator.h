#ifndef FAMI32_PSRAM_ALLOCATOR_H
#define FAMI32_PSRAM_ALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <vector>

#ifdef FAMI32_DESKTOP

template <typename T>
using PsramVector = std::vector<T>;

#else

#include "esp_heap_caps.h"

/*
 * Allocator for persistent, cacheable working buffers. These buffers are not
 * DMA descriptors, FreeRTOS objects, task stacks, or data used while the flash
 * cache is disabled, so keeping them in PSRAM preserves scarce internal RAM.
 * FAMI32 requires PSRAM; allocation failure is therefore a fatal bring-up
 * error instead of silently falling back to internal RAM.
 */
template <typename T>
class PsramAllocator {
public:
    using value_type = T;
    using is_always_equal = std::true_type;

    PsramAllocator() noexcept = default;

    template <typename U>
    PsramAllocator(const PsramAllocator<U> &) noexcept {}

    T *allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            std::abort();
        }
        void *memory = heap_caps_malloc(
            count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (memory == nullptr && count != 0) {
            std::abort();
        }
        return static_cast<T *>(memory);
    }

    void deallocate(T *memory, std::size_t) noexcept {
        heap_caps_free(memory);
    }
};

template <typename T, typename U>
bool operator==(const PsramAllocator<T> &, const PsramAllocator<U> &) noexcept {
    return true;
}

template <typename T, typename U>
bool operator!=(const PsramAllocator<T> &, const PsramAllocator<U> &) noexcept {
    return false;
}

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

#endif

#endif
