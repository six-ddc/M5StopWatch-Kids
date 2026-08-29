/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_hanzi.h"
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <assets/hanzi/hanzi_data.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>

using namespace mooncake;

namespace {

// Target frame interval. Playback is time-based, so a late frame catches up
// rather than slowing the animation down.
constexpr uint32_t kFrameIntervalMs = 33;
constexpr uint32_t kKeyPollMs       = 25;

constexpr const char* kNvsNamespace = "hanzi";
constexpr const char* kNvsLastChar  = "last";

}  // namespace

AppHanzi::AppHanzi()
{
    setAppInfo().name = "识字";
    setAppInfo().icon = (void*)&icon_hanzi;
}

void AppHanzi::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppHanzi::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    if (!_source.bind(hanzi_data_blob, hanzi_data_blob_size)) {
        mclog::tagError(getAppInfo().name, "stroke data failed validation");
        close();
        return;
    }
    mclog::tagInfo(getAppInfo().name, "{} characters, {} lessons", _source.charCount(),
                   _source.lessonCount());

    uint16_t start = 0;
    {
        Settings settings(kNvsNamespace, false);
        const int stored = settings.GetInt(kNvsLastChar, 0);
        if (stored > 0 && stored < _source.charCount()) {
            start = static_cast<uint16_t>(stored);
        }
    }

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _learn = std::make_unique<view::LearnPage>();
    if (!_learn->create(lv_screen_active(), &_source)) {
        mclog::tagError(getAppInfo().name, "learn page failed to initialise");
        _learn.reset();
        close();
        return;
    }

    _browse = std::make_unique<view::BrowsePage>();
    if (!_browse->create(lv_screen_active(), &_source,
                         [this](uint16_t order) { openLearn(order); })) {
        mclog::tagError(getAppInfo().name, "browse page failed to initialise");
        _browse.reset();
        _learn.reset();
        close();
        return;
    }

    _learn->showCharacter(start);
    _browse->focusCharacter(start);
    showBrowse();
    _last_tick_ms = GetHAL().millis();
}

void AppHanzi::showBrowse()
{
    _page = Page::Browse;
    if (_browse) {
        _browse->setHidden(false);
    }
    if (_learn) {
        _learn->setHidden(true);
    }
}

void AppHanzi::openLearn(uint16_t order)
{
    // Reached from the browse page's click handler, which runs inside
    // lv_timer_handler with the LVGL lock already held: no locking here, and no
    // NVS write either -- that is deferred to onRunning.
    _page = Page::Learn;
    if (_learn) {
        _learn->showCharacter(order);
        _learn->setHidden(false);
    }
    if (_browse) {
        _browse->setHidden(true);
    }
    _last_tick_ms   = GetHAL().millis();
    _progress_dirty = true;
}

void AppHanzi::saveProgress()
{
    if (!_learn) {
        return;
    }
    // Writing NVS turns the flash cache off and stalls the other core, so this
    // must never run while holding the LVGL lock or inside a draw callback.
    Settings settings(kNvsNamespace, true);
    settings.SetInt(kNvsLastChar, static_cast<int>(_learn->order()));
    _progress_dirty = false;
}

void AppHanzi::onRunning()
{
    if (!_key_manager) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();

    // Polling the buttons costs an I2C transaction. The main loop spins far
    // faster than a human can press, and every poll competes with the frame
    // work below, so throttle it to 40 Hz.
    if (now_ms - _last_key_ms < kKeyPollMs) {
        tickAnimation(now_ms);
        return;
    }
    _last_key_ms = now_ms;

    switch (_key_manager->update()) {
        case input::KeyEvent::GoHome:
            if (_page == Page::Learn && _browse && _learn) {
                // Back out to the lesson grid first; only a second press leaves
                // the app.
                {
                    LvglLockGuard lock;
                    _browse->focusCharacter(_learn->order());
                    showBrowse();
                }
                saveProgress();  // outside the lock: NVS stalls the cache
            } else {
                // The launcher reopens itself once this app reaches Sleeping,
                // so closing is the whole of "go home".
                close();
            }
            return;

        case input::KeyEvent::GoPrevious: {
            if (!_browse || !_learn) {
                break;
            }
            LvglLockGuard lock;
            if (_page == Page::Learn) {
                _learn->previous();
            } else {
                _browse->previousPage();
            }
            break;
        }

        case input::KeyEvent::GoNext: {
            if (!_browse || !_learn) {
                break;
            }
            LvglLockGuard lock;
            if (_page == Page::Learn) {
                _learn->next();
            } else {
                _browse->nextPage();
            }
            break;
        }

        default:
            break;
    }

    // Deferred from a click handler, where NVS must not be touched.
    if (_progress_dirty) {
        saveProgress();
    }

    tickAnimation(now_ms);
}

void AppHanzi::tickAnimation(uint32_t now_ms)
{
    if (_page != Page::Learn || !_learn || !_learn->ready()) {
        return;
    }
    const uint32_t dt = now_ms - _last_tick_ms;
    if (dt < kFrameIntervalMs) {
        return;
    }
    _last_tick_ms = now_ms;

    {
        LvglLockGuard lock;
        _learn->update(dt);
    }

    // A small rising two-note cue when the character is finished: positive
    // feedback for a child, and it marks the loop boundary. Kept out of the
    // lock because tone synthesis builds a PCM buffer.
    if (_learn->takeCharCompleted()) {
        audio::play_melody({72, 76}, 0.09f, 0.35f);
    }
}

void AppHanzi::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    saveProgress();
    _key_manager.reset();

    LvglLockGuard lock;
    _browse.reset();
    _learn.reset();
}
