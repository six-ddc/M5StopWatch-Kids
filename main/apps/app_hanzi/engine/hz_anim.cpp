/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hz_anim.h"

namespace hz {

void Animator::begin(const Character* character, const AnimConfig& cfg)
{
    _ch  = character;
    _cfg = cfg;
    restart();
}

void Animator::restart()
{
    _phase          = (_ch != nullptr && _ch->stroke_count > 0) ? Phase::Intro : Phase::Idle;
    _stroke         = 0;
    _reveal_from    = 0.0f;
    _reveal_to      = 0.0f;
    _timer_ms       = 0;
    _stroke_landed  = false;
    _stroke_started = false;
    _char_completed = false;
}

void Animator::enterStroke(uint16_t index)
{
    _stroke         = index;
    _reveal_from    = 0.0f;
    _reveal_to      = 0.0f;
    _timer_ms       = 0;
    _phase          = Phase::Reveal;
    _stroke_started = true;
}

bool Animator::tick(uint32_t dt_ms)
{
    _stroke_landed  = false;
    _stroke_started = false;
    _char_completed = false;
    if (_phase == Phase::Idle || _ch == nullptr || dt_ms == 0) {
        return false;
    }

    switch (_phase) {
        case Phase::Intro:
            // The ghost outline is static during the intro, so nothing needs
            // redrawing until the first stroke starts.
            _timer_ms += dt_ms;
            if (_timer_ms >= _cfg.intro_ms) {
                enterStroke(0);
                return true;
            }
            return false;

        case Phase::Reveal: {
            const Stroke& st = _ch->strokes[_stroke];
            const float step = _cfg.speed_px_per_sec * _cfg.rate *
                               (static_cast<float>(dt_ms) / 1000.0f);
            _reveal_from = _reveal_to;
            _reveal_to += step;
            if (_reveal_to >= st.median_length) {
                _reveal_to     = st.median_length;
                _phase         = Phase::Pause;
                _timer_ms      = 0;
                _stroke_landed = true;
            }
            return true;
        }

        case Phase::Pause:
            _timer_ms += dt_ms;
            if (_timer_ms >= _cfg.stroke_pause_ms) {
                if (_stroke + 1 < _ch->stroke_count) {
                    enterStroke(static_cast<uint16_t>(_stroke + 1));
                } else {
                    _phase          = Phase::Done;
                    _timer_ms       = 0;
                    _char_completed = true;
                }
                return true;
            }
            return false;

        case Phase::Done:
            _timer_ms += dt_ms;
            if (_cfg.loop && _timer_ms >= _cfg.char_done_ms) {
                restart();
                return true;
            }
            return false;

        default:
            return false;
    }
}

}  // namespace hz
