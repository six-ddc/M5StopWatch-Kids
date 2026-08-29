/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include "hz_data.h"

// Playback timing for one character. Pure state machine: it owns no pixels and
// no timers, the caller feeds it elapsed milliseconds.

namespace hz {

enum class Phase : uint8_t {
    Idle,    // nothing loaded
    Intro,   // ghost outline fading in, before the first stroke
    Reveal,  // a stroke is being written
    Pause,   // brief hold after a stroke lands
    Done,    // whole character held complete
};

struct AnimConfig {
    float speed_px_per_sec = 250.0f;  // writing speed at rate 1.0
    float rate             = 1.0f;    // user-selectable multiplier
    uint16_t intro_ms      = 300;
    uint16_t stroke_pause_ms = 400;
    uint16_t char_done_ms    = 1500;
    bool loop                = true;
};

class Animator {
public:
    void begin(const Character* character, const AnimConfig& cfg);
    void restart();
    void setConfig(const AnimConfig& cfg)
    {
        _cfg = cfg;
    }
    const AnimConfig& config() const
    {
        return _cfg;
    }

    // Advances playback. Returns true when the visible state changed, i.e. the
    // caller should redraw. Sub-millisecond ticks are accumulated, not dropped.
    bool tick(uint32_t dt_ms);

    Phase phase() const
    {
        return _phase;
    }
    uint16_t strokeIndex() const
    {
        return _stroke;
    }
    // Arc length already written on the current stroke, in pixels.
    float revealFrom() const
    {
        return _reveal_from;
    }
    float revealTo() const
    {
        return _reveal_to;
    }
    // One-shot, true on the tick a stroke finishes (drives haptics / sfx).
    bool strokeJustLanded() const
    {
        return _stroke_landed;
    }
    // One-shot, true on the tick a new stroke becomes current; the caller must
    // rasterise it before drawing.
    bool strokeJustStarted() const
    {
        return _stroke_started;
    }
    // One-shot, true on the tick the whole character finishes.
    bool charJustCompleted() const
    {
        return _char_completed;
    }
    // True while a stroke index is meaningful for drawing.
    bool hasActiveStroke() const
    {
        return _phase == Phase::Reveal || _phase == Phase::Pause;
    }

private:
    void enterStroke(uint16_t index);

    const Character* _ch = nullptr;
    AnimConfig _cfg;
    Phase _phase        = Phase::Idle;
    uint16_t _stroke    = 0;
    float _reveal_from  = 0.0f;
    float _reveal_to    = 0.0f;
    uint32_t _timer_ms   = 0;
    bool _stroke_landed  = false;
    bool _stroke_started = false;
    bool _char_completed = false;
};

}  // namespace hz
