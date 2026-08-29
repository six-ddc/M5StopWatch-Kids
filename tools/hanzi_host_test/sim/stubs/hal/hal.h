/*
 * Host-sim stub for main/hal/hal.h.
 *
 * Only the surface that main/apps/app_hanzi/view/browse.cpp and learn.cpp
 * actually touch:
 * GetHAL().lvglLock()/lvglUnlock()/millis()/vibrate(), and LvglLockGuard.
 * The real hal.h pulls in LVGL transitively, so this one does too.
 */
#pragma once
#include <lvgl.h>

#include <chrono>
#include <cstdint>
#include <cstdio>

class Hal {
public:
    bool lvglLock()
    {
        return true;
    }
    void lvglUnlock() {}

    std::uint32_t millis()
    {
        using namespace std::chrono;
        static const auto start = steady_clock::now();
        return static_cast<std::uint32_t>(
            duration_cast<milliseconds>(steady_clock::now() - start).count());
    }

    void vibrate(uint16_t durationMs, uint8_t strength = 100)
    {
        std::fprintf(stderr, "[sim] vibrate(%u ms, strength=%u)\n", durationMs, strength);
    }
};

inline Hal& GetHAL()
{
    static Hal hal;
    return hal;
}

class LvglLockGuard {
public:
    LvglLockGuard()
    {
        GetHAL().lvglLock();
    }
    ~LvglLockGuard()
    {
        GetHAL().lvglUnlock();
    }
};
