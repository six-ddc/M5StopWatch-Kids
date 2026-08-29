/*
 * Host-sim stub for ESP-IDF's esp_heap_caps.h.
 *
 * heap_caps_malloc on real hardware does not zero the allocation, which is
 * exactly the "uninitialised PSRAM shows up as speckle" bug class the view
 * code works around (see learn.cpp's allocPsram comment). Filling with 0x5A
 * here instead of zeroing reproduces that on the host: any code path that
 * forgets to initialise a buffer it owns will show it.
 */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define MALLOC_CAP_SPIRAM (static_cast<uint32_t>(1u << 10))
#define MALLOC_CAP_8BIT (static_cast<uint32_t>(1u << 2))
#define MALLOC_CAP_DEFAULT (static_cast<uint32_t>(0))

inline void* heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    void* p = std::malloc(size);
    if (p != nullptr) {
        std::memset(p, 0x5A, size);
    }
    return p;
}

inline void heap_caps_free(void* ptr)
{
    std::free(ptr);
}
