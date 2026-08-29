/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_hanzi.h"
#include <apps/common/audio/audio.h>
#include <apps/common/pinyin_ime/py_normalize.h>
#include <assets/assets.h>
#include <assets/hanzi/hanzi_data.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <cstring>
#include <string>
#include <vector>

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
#if !KIDS_STANDALONE
    // Only a build with a launcher has anything to draw the icon on;
    // a single-app build leaves the image out of the firmware entirely,
    // so referencing it here would not even link.
    setAppInfo().icon = (void*)&icon_hanzi;
#endif
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
    mclog::tagInfo(getAppInfo().name, "{} characters, {} lessons", _source.charCount(), _source.lessonCount());

    uint16_t start = 0;
    {
        Settings settings(kNvsNamespace, false);
        const int stored = settings.GetInt(kNvsLastChar, 0);
        if (stored > 0 && stored < _source.charCount()) {
            start = static_cast<uint16_t>(stored);
        }
    }

    _key_manager = std::make_unique<input::KeyManager>();

    // Build the T9 index before taking the LVGL lock: it is pure string
    // crunching and nothing on the UI thread needs to wait for it.
    if (!buildSearchIndex()) {
        mclog::tagError(getAppInfo().name, "pinyin index failed to build");
        close();
        return;
    }

    LvglLockGuard lock;

    _learn = std::make_unique<view::LearnPage>();
    if (!_learn->create(lv_screen_active(), &_source)) {
        mclog::tagError(getAppInfo().name, "learn page failed to initialise");
        _learn.reset();
        close();
        return;
    }

    _browse = std::make_unique<view::BrowsePage>();
    if (!_browse->create(
            lv_screen_active(), &_source, [this](uint16_t order) { openLearn(order, LearnFrom::Browse); },
            [this]() { showSearch(); })) {
        mclog::tagError(getAppInfo().name, "browse page failed to initialise");
        _browse.reset();
        _learn.reset();
        close();
        return;
    }

    _search = std::make_unique<view::SearchPage>();
    // No browse callback for now: on the real glass a ring scrub reads as a
    // horizontal swipe often enough that the mode-switch gesture fights the
    // dial, so the browse entry is parked until it gets a better trigger.
    // (SearchPage keeps the capability; pass a callback to re-enable.)
    if (!_search->create(lv_screen_active(), &_source, &_engine)) {
        mclog::tagError(getAppInfo().name, "search page failed to initialise");
        _search.reset();
        _browse.reset();
        _learn.reset();
        close();
        return;
    }

    _learn->showCharacter(start);
    _browse->focusCharacter(start);
    // Search is the landing page: a child usually arrives knowing the sound
    // of a character; the textbook browse mode is one corner tap away.
    showSearch();
    _last_tick_ms = GetHAL().millis();
}

bool AppHanzi::buildSearchIndex()
{
    const uint32_t t0 = GetHAL().millis();
    // Each reading of each character becomes one (toneless syllable, order)
    // entry; the engine copies everything, so the staging vectors are local.
    // Entries reference the strings only after both vectors stop growing.
    std::vector<std::string> texts;
    std::vector<uint16_t> ids;
    texts.reserve(_source.charCount() + _source.charCount() / 4);
    for (uint16_t order = 0; order < _source.charCount(); ++order) {
        const char* p = _source.pinyinAt(order);
        while (*p != '\0') {
            const char* start = p;
            while (*p != '\0' && *p != ' ') {
                p++;
            }
            char token[24];
            const size_t len = static_cast<size_t>(p - start);
            if (len < sizeof(token)) {
                std::memcpy(token, start, len);
                token[len] = '\0';
                char plain[24];
                if (pime::pyNormalize(token, plain, sizeof(plain)) > 0) {
                    texts.emplace_back(plain);
                    ids.push_back(order);
                } else {
                    mclog::tagWarn(getAppInfo().name, "unusable reading for char {}", order);
                }
            }
            while (*p == ' ') {
                p++;
            }
        }
    }
    std::vector<pime::Entry> entries;
    entries.reserve(texts.size());
    for (size_t i = 0; i < texts.size(); ++i) {
        entries.push_back({texts[i].c_str(), ids[i]});
    }
    if (!_engine.build(entries.data(), entries.size())) {
        return false;
    }
    mclog::tagInfo(getAppInfo().name, "pinyin index: {} readings in {} ms", entries.size(), GetHAL().millis() - t0);
    return true;
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
    if (_search) {
        _search->setHidden(true);
    }
}

void AppHanzi::showSearch()
{
    // Also reached from the browse page's click handler (lock already held,
    // see openLearn); only hidden-flag flips happen here. The search page
    // keeps its input state, so coming back resumes where the child left off.
    _page = Page::Search;
    if (_search) {
        _search->setHidden(false);
    }
    if (_browse) {
        _browse->setHidden(true);
    }
    if (_learn) {
        _learn->setHidden(true);
    }
}

void AppHanzi::openLearn(uint16_t order, LearnFrom from, const char* reading)
{
    // Reached from the browse page's click handler, which runs inside
    // lv_timer_handler with the LVGL lock already held: no locking here, and no
    // NVS write either -- that is deferred to onRunning.
    _page       = Page::Learn;
    _learn_from = from;
    if (_learn) {
        _learn->showCharacter(order, reading);
        _learn->setHidden(false);
    }
    if (_browse) {
        _browse->setHidden(true);
    }
    if (_search) {
        _search->setHidden(true);
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

    // A tapped candidate is recorded in the click handler and consumed here,
    // where taking the lock is allowed (same pattern as the math/english
    // apps' takeTap).
    if (_page == Page::Search && _search) {
        uint16_t order = 0;
        char reading[16];
        if (_search->takePick(order, reading, sizeof(reading))) {
            LvglLockGuard lock;
            openLearn(order, LearnFrom::Search, reading[0] != '\0' ? reading : nullptr);
        }
    }

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
                // Back out to where the character came from; only a second
                // press leaves the app.
                {
                    LvglLockGuard lock;
                    if (_learn_from == LearnFrom::Search && _search) {
                        showSearch();
                    } else {
                        _browse->focusCharacter(_learn->order());
                        showBrowse();
                    }
                }
                saveProgress();  // outside the lock: NVS stalls the cache
            } else if (_page == Page::Browse) {
                // Search is the app's landing page, so browse backs out to it.
                LvglLockGuard lock;
                showSearch();
            } else {
#if !KIDS_STANDALONE
                // The launcher reopens itself once this app reaches Sleeping,
                // so closing is the whole of "go home".
                close();
#endif
                // In a single-app build there is nothing to go home to, and
                // nothing would reopen this app -- so the press does nothing.
            }
            return;

        case input::KeyEvent::GoPrevious: {
            if (!_browse || !_learn) {
                break;
            }
            LvglLockGuard lock;
            if (_page == Page::Learn) {
                // From the textbook, neighbours are the lesson sequence and
                // worth cruising. A searched character has no meaningful
                // neighbours (the tail is frequency-sorted), so A goes
                // straight back to the search results instead.
                if (_learn_from == LearnFrom::Search && _search) {
                    showSearch();
                } else {
                    _learn->previous();
                }
            } else if (_page == Page::Search) {
                _search->previousCandidatePage();
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
                // Searched characters: B replays the stroke order (same as
                // tapping the canvas), the natural "write it again".
                if (_learn_from == LearnFrom::Search) {
                    _learn->replay();
                } else {
                    _learn->next();
                }
            } else if (_page == Page::Search) {
                _search->nextCandidatePage();
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
    _search.reset();
    _browse.reset();
    _learn.reset();
    _engine.clear();
}
