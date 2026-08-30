/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <esp_heap_caps.h>
#include <mooncake_log.h>
#include <cstring>
#include "view.h"

namespace view {

namespace {

constexpr const char* kTag = "AppHanzi";

constexpr size_t kArenaBytes = 12 * 1024;

// Mode-switch slide: the two pages travel one screen width in the swipe
// direction. Short enough to read as one motion, long enough to show where
// the old page went (so a child can swipe it right back).
constexpr int32_t kSlideDist  = 466;
constexpr uint32_t kSlideMs   = 220;
constexpr uint16_t kMaxPrefix = 15;

void modeGestureCb(lv_event_t* e)
{
    auto* page      = static_cast<view::SearchPage*>(lv_event_get_user_data(e));
    lv_indev_t* dev = lv_indev_active();
    if (page == nullptr || dev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(dev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    // The live mode may own this press already: a vertical wheel drag, a
    // ring scrub or a resting keypad finger is input, never a page switch.
    if (page->gestureBusy()) {
        return;
    }
    // Swallow the rest of the press so whatever is under the finger does
    // not also fire a click on release.
    lv_indev_wait_release(dev);
    page->handleModeSwipe(dir == LV_DIR_LEFT ? 1 : -1);
}

}  // namespace

// Renders candidate glyphs the same way the browse grid does: decode into an
// arena, rasterise into the caller's A8 buffer. Captions come straight from
// the blob's pinyin strings.
class SearchPage::HanziPainter : public pime::GlyphPainter {
public:
    ~HanziPainter()
    {
        release();
    }

    bool init(const hz::DataSource* source, uint16_t max_glyph)
    {
        _src            = source;
        _scratch_floats = static_cast<size_t>(max_glyph) + 320;
        _arena_mem      = static_cast<uint8_t*>(heap_caps_malloc(kArenaBytes, MALLOC_CAP_SPIRAM));
        _scratch        = static_cast<float*>(heap_caps_malloc(_scratch_floats * sizeof(float), MALLOC_CAP_SPIRAM));
        if (_arena_mem == nullptr || _scratch == nullptr) {
            release();
            return false;
        }
        std::memset(_arena_mem, 0, kArenaBytes);
        std::memset(_scratch, 0, _scratch_floats * sizeof(float));
        _arena = hz::Arena(_arena_mem, kArenaBytes);
        return true;
    }

    void release()
    {
        if (_arena_mem != nullptr) {
            heap_caps_free(_arena_mem);
            _arena_mem = nullptr;
        }
        if (_scratch != nullptr) {
            heap_caps_free(_scratch);
            _scratch = nullptr;
        }
        _arena = hz::Arena();
        _src   = nullptr;
    }

    bool paint(uint16_t id, uint8_t* buffer, uint16_t w, uint16_t h) override
    {
        if (_src == nullptr || _scratch == nullptr || id >= _src->charCount()) {
            return false;
        }
        hz::Transform tf;
        tf.scale = (static_cast<float>(w) / static_cast<float>(_src->coordScale())) * 0.94f;
        tf.ox    = 0.5f * (w - _src->coordScale() * tf.scale);
        tf.oy    = 0.5f * (h - _src->coordScale() * tf.scale);

        _arena.reset();
        hz::Character ch;
        if (!_src->decode(id, tf, ch, _arena)) {
            mclog::tagWarn(kTag, "search: decode failed for {}", id);
            return false;
        }
        hz::Rasterizer raster(_scratch, _scratch_floats);
        hz::Mask mask;
        mask.data   = buffer;
        mask.x0     = 0;
        mask.y0     = 0;
        mask.w      = w;
        mask.h      = h;
        mask.stride = w;
        hz::Compositor::renderStatic(ch, raster, mask);
        return true;
    }

    const char* caption(uint16_t id) const override
    {
        return _src != nullptr ? _src->pinyinAt(id) : nullptr;
    }

private:
    const hz::DataSource* _src = nullptr;
    uint8_t* _arena_mem        = nullptr;
    float* _scratch            = nullptr;
    size_t _scratch_floats     = 0;
    hz::Arena _arena;
};

SearchPage::SearchPage() = default;

SearchPage::~SearchPage()
{
    destroy();
}

bool SearchPage::create(lv_obj_t* parent, const hz::DataSource* source, const pime::T9Engine* engine)
{
    if (source == nullptr || !source->valid() || engine == nullptr) {
        return false;
    }
    _painter = std::make_unique<HanziPainter>();
    // 68 == the picker's glyph box, the largest of the three modes; the
    // painter only needs it for scratch sizing, so an over-estimate would
    // merely waste a few floats.
    if (!_painter->init(source, 68) || !_picker.create(parent, engine, _painter.get()) ||
        !_dial.create(parent, engine, _painter.get()) || !_ime.create(parent, engine, _painter.get())) {
        destroy();
        return false;
    }

    // Mode switch: a wide horizontal swipe anywhere on the page cycles the
    // three input modes. The bubble flag must be cleared on each mode root:
    // LVGL delivers a gesture to the first ancestor of the pressed object
    // without it, or drops the event entirely when the walk runs past the
    // screen.
    for (lv_obj_t* root : {_picker.root(), _dial.root(), _ime.root()}) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(root, modeGestureCb, LV_EVENT_GESTURE, this);
    }

    _mode = Mode::Picker;
    applyVisibility();
    return true;
}

void SearchPage::destroy()
{
    _picker.destroy();
    _dial.destroy();
    _ime.destroy();
    _painter.reset();
    _mode    = Mode::Picker;
    _sliding = false;
}

lv_obj_t* SearchPage::modeRoot(Mode m)
{
    switch (m) {
        case Mode::Dial:
            return _dial.root();
        case Mode::Keypad:
            return _ime.root();
        default:
            return _picker.root();
    }
}

void SearchPage::applyVisibility()
{
    // Idempotent ground truth: only the live mode shows while the page is
    // visible. Also the cleanup path when a slide is cut short -- any
    // leftover translation is cleared with the animations.
    for (uint8_t m = 0; m < kModeCount; m++) {
        lv_obj_t* root = modeRoot(static_cast<Mode>(m));
        lv_anim_delete(root, slideExecCb);
        lv_obj_set_style_translate_x(root, 0, 0);
        if (!_hidden && static_cast<Mode>(m) == _mode) {
            lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        }
    }
    _sliding = false;
}

void SearchPage::setHidden(bool hidden)
{
    _hidden = hidden;
    applyVisibility();
}

void SearchPage::carryState(Mode from, Mode to)
{
    // The carrier is a toneless letter prefix plus the selected candidate's
    // id (-1 for none): enough to rebuild any mode's input state, and every
    // mode can produce it. The digit string is never carried -- it is
    // ambiguous, the letter prefix is not.
    char prefix[kMaxPrefix + 1] = {};
    int32_t id                  = -1;
    switch (from) {
        case Mode::Picker:
            _picker.exportState(prefix, sizeof(prefix), id);
            break;
        case Mode::Dial:
            _dial.exportState(prefix, sizeof(prefix), id);
            break;
        case Mode::Keypad:
            _ime.exportState(prefix, sizeof(prefix), id);
            break;
    }
    switch (to) {
        case Mode::Picker:
            _picker.importState(prefix, id);
            break;
        case Mode::Dial:
            _dial.importState(prefix, id);
            break;
        case Mode::Keypad:
            _ime.importState(prefix, id);
            break;
    }
}

void SearchPage::switchMode(Mode to, int8_t dir, bool animate)
{
    if (to == _mode || _picker.root() == nullptr) {
        return;
    }
    carryState(_mode, to);
    lv_obj_t* old_root = modeRoot(_mode);
    lv_obj_t* new_root = modeRoot(to);
    _mode              = to;
    _mode_dirty        = true;
    if (!animate || _hidden) {
        applyVisibility();
        return;
    }

    // Both pages travel together in the swipe direction: the old one slides
    // out, the new one slides in from the opposite edge. Plain translation
    // only -- no scaling or rotation, so neither page pays for an
    // intermediate render layer.
    _sliding = true;
    lv_obj_set_style_translate_x(new_root, dir * kSlideDist, 0);
    lv_obj_clear_flag(new_root, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t out;
    lv_anim_init(&out);
    lv_anim_set_var(&out, old_root);
    lv_anim_set_exec_cb(&out, slideExecCb);
    lv_anim_set_values(&out, 0, -dir * kSlideDist);
    lv_anim_set_duration(&out, kSlideMs);
    lv_anim_set_path_cb(&out, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&out, slideOutDoneCb);
    lv_anim_start(&out);

    lv_anim_t in;
    lv_anim_init(&in);
    lv_anim_set_var(&in, new_root);
    lv_anim_set_exec_cb(&in, slideExecCb);
    lv_anim_set_values(&in, dir * kSlideDist, 0);
    lv_anim_set_duration(&in, kSlideMs);
    lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&in, slideInDoneCb);
    lv_anim_set_user_data(&in, this);
    lv_anim_start(&in);
}

void SearchPage::slideExecCb(void* obj, int32_t v)
{
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), v, 0);
}

void SearchPage::slideOutDoneCb(lv_anim_t* anim)
{
    auto* root = static_cast<lv_obj_t*>(anim->var);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_x(root, 0, 0);
}

void SearchPage::slideInDoneCb(lv_anim_t* anim)
{
    auto* page = static_cast<SearchPage*>(lv_anim_get_user_data(anim));
    if (page != nullptr) {
        page->_sliding = false;
    }
}

void SearchPage::handleModeSwipe(int8_t dir)
{
    GetHAL().vibrate(15);
    const uint8_t next = static_cast<uint8_t>((mode() + kModeCount + dir) % kModeCount);
    switchMode(static_cast<Mode>(next), dir, true);
}

void SearchPage::setMode(uint8_t mode)
{
    if (mode >= kModeCount) {
        return;
    }
    switchMode(static_cast<Mode>(mode), 1, false);
    // Restoring the stored mode is not a change worth writing back.
    _mode_dirty = false;
}

bool SearchPage::takeModeDirty(uint8_t& mode)
{
    if (!_mode_dirty) {
        return false;
    }
    _mode_dirty = false;
    mode        = static_cast<uint8_t>(_mode);
    return true;
}

bool SearchPage::gestureBusy() const
{
    if (_sliding) {
        return true;
    }
    switch (_mode) {
        case Mode::Dial:
            return _dial.ringActive();
        case Mode::Keypad:
            return _ime.keyActive();
        default:
            return _picker.scrollActive();
    }
}

void SearchPage::showCharacter(uint16_t order)
{
    _picker.showCharacter(order);
    if (_mode != Mode::Picker) {
        // The picker resolved the character to a syllable and a list
        // position; hand that to the live mode through the usual carrier.
        carryState(Mode::Picker, _mode);
    }
}

void SearchPage::nextCandidatePage()
{
    switch (_mode) {
        case Mode::Dial:
            _dial.nextCandidatePage();
            break;
        case Mode::Keypad:
            _ime.nextCandidatePage();
            break;
        default:
            _picker.candidateStep(1);
            break;
    }
}

void SearchPage::previousCandidatePage()
{
    switch (_mode) {
        case Mode::Dial:
            _dial.previousCandidatePage();
            break;
        case Mode::Keypad:
            _ime.previousCandidatePage();
            break;
        default:
            _picker.candidateStep(-1);
            break;
    }
}

bool SearchPage::takePick(uint16_t& order, char* reading, size_t cap)
{
    // Only the live mode can hold a pending pick, but draining all three
    // costs nothing and survives a pick racing a mode switch.
    return _picker.takePick(order, reading, cap) || _dial.takePick(order, reading, cap) ||
           _ime.takePick(order, reading, cap);
}

}  // namespace view
