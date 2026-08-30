/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ime_view.h"

#include <assets/assets.h>
#include <esp_heap_caps.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <cstdio>
#include <cstring>

#include "py_normalize.h"

namespace pime {

namespace {

// Layout (LV_ALIGN_CENTER offsets, 466 px round screen, radius 233). The
// A/B guides and the page indicator share one line hugging the top rim --
// A on the left half, B on the right, pointing at their physical keys at
// the 10 and 2 o'clock marks. (Rotated arc-tangent guides under the keys
// were tried first: a 41-degree label's bounding box is ~68 px tall and
// collides with any full-width chip row.) That frees the whole band above
// the chips, so the content stack moves up and the keypad grows to
// child-finger size.
constexpr int16_t kGuideX     = 78;
constexpr int16_t kGuideY     = -184;  // corners stay at r<=229
constexpr int16_t kPageY      = -184;
constexpr int16_t kChipY      = -140;  // pinyin interpretation chips (32 px)
constexpr int16_t kEmptyY     = -115;  // empty-state prompt
constexpr int16_t kCandY      = -66;   // character candidate strip
constexpr int16_t kCandStep   = 72;
constexpr int16_t kCandChipW  = 68;
constexpr int16_t kCandChipH  = 92;
constexpr int16_t kKeyW       = 82;
constexpr int16_t kKeyH       = 60;
constexpr int16_t kKeyStepX   = 92;
constexpr int16_t kKeyRowY[3] = {22, 88, 154};
constexpr int16_t kChipH      = 44;
constexpr int16_t kChipPadX   = 10;
constexpr int16_t kChipGap    = 8;

constexpr uint32_t kInk    = 0xF2F0EA;
constexpr uint32_t kGrey   = 0x8A8A88;
constexpr uint32_t kKeyBg  = 0x22221F;
constexpr uint32_t kChipBg = 0x33322F;
constexpr uint32_t kCandBg = 0x262624;
// Pressed-state lift for every tappable surface: bright enough to read as
// "this one", dim enough not to flash on an AMOLED in a dark room.
constexpr uint32_t kPressedBg = 0x45443F;
constexpr uint32_t kReject    = 0xB03A2E;

constexpr const char* kKeyCaps[] = {"abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};

// The rejected-key flash restores itself through a one-shot timer; the timer
// runs inside lv_timer_handler, so the lock is already held there.
void rejectTimerCb(lv_timer_t* timer)
{
    auto* key = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
    if (key != nullptr) {
        lv_obj_set_style_border_width(key, 0, 0);
    }
    lv_timer_delete(timer);
}

void letterKeyCb(lv_event_t* e);
void deleteShortCb(lv_event_t* e);
void deleteLongCb(lv_event_t* e);
void interpChipCb(lv_event_t* e);
void candidateCb(lv_event_t* e);
void keyStateCb(lv_event_t* e);

}  // namespace

ImeView::~ImeView()
{
    destroy();
}

bool ImeView::allocate()
{
    const size_t glyph_px = static_cast<size_t>(kCandGlyph) * kCandGlyph;
    for (Cand& c : _cands) {
        c.buffer = static_cast<uint8_t*>(heap_caps_malloc(glyph_px, MALLOC_CAP_SPIRAM));
        if (c.buffer == nullptr) {
            return false;
        }
        std::memset(c.buffer, 0, glyph_px);
    }
    return true;
}

void ImeView::release()
{
    for (Cand& c : _cands) {
        if (c.buffer != nullptr) {
            lv_image_cache_drop(&c.dsc);
            heap_caps_free(c.buffer);
            c.buffer = nullptr;
        }
    }
}

bool ImeView::create(lv_obj_t* parent, const T9Engine* source, GlyphPainter* painter)
{
    if (source == nullptr || painter == nullptr || !allocate()) {
        release();
        return false;
    }
    _source  = source;
    _painter = painter;

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    auto makeGuide = [&](const char* text) {
        lv_obj_t* label = lv_label_create(_root);
        lv_obj_set_style_text_font(label, &lv_font_hanzi_ui_24, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kGrey), 0);
        lv_label_set_text(label, text);
        return label;
    };
    _hint_a = makeGuide("A 上页");
    lv_obj_align(_hint_a, LV_ALIGN_CENTER, -kGuideX, kGuideY);
    _hint_b = makeGuide("B 下页");
    lv_obj_align(_hint_b, LV_ALIGN_CENTER, kGuideX, kGuideY);
    _page_label = makeGuide("");
    lv_obj_align(_page_label, LV_ALIGN_CENTER, 0, kPageY);

    _empty_hint = lv_label_create(_root);
    lv_obj_set_style_text_font(_empty_hint, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_empty_hint, lv_color_hex(kGrey), 0);
    lv_label_set_text(_empty_hint, "想学哪个字?");
    lv_obj_align(_empty_hint, LV_ALIGN_CENTER, 0, kEmptyY);

    for (uint8_t i = 0; i < kInterpSlots; i++) {
        InterpChip& chip = _interp_chips[i];
        chip.chip        = lv_obj_create(_root);
        lv_obj_remove_style_all(chip.chip);
        lv_obj_set_style_radius(chip.chip, 14, 0);
        lv_obj_set_style_bg_color(chip.chip, lv_color_hex(kChipBg), 0);
        lv_obj_set_style_bg_color(chip.chip, lv_color_hex(kPressedBg), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(chip.chip, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_height(chip.chip, kChipH);
        lv_obj_clear_flag(chip.chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip.chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(chip.chip, 6);
        lv_obj_add_event_cb(chip.chip, interpChipCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(chip.chip, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        chip.label = lv_label_create(chip.chip);
        lv_obj_set_style_text_font(chip.label, &lv_font_pinyin_latin_32, 0);
        lv_obj_center(chip.label);
    }

    for (uint8_t i = 0; i < kCandCells; i++) {
        Cand& c = _cands[i];
        c.chip  = lv_obj_create(_root);
        lv_obj_remove_style_all(c.chip);
        lv_obj_set_style_radius(c.chip, 14, 0);
        lv_obj_set_style_bg_color(c.chip, lv_color_hex(kCandBg), 0);
        lv_obj_set_style_bg_color(c.chip, lv_color_hex(kPressedBg), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(c.chip, LV_OPA_COVER, 0);
        lv_obj_set_size(c.chip, kCandChipW, kCandChipH);
        lv_obj_clear_flag(c.chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c.chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(c.chip, 4);
        lv_obj_add_event_cb(c.chip, candidateCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(c.chip, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_align(c.chip, LV_ALIGN_CENTER, static_cast<int16_t>((i - 2) * kCandStep), kCandY);

        std::memset(&c.dsc, 0, sizeof(c.dsc));
        c.dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        c.dsc.header.cf     = LV_COLOR_FORMAT_A8;
        c.dsc.header.w      = kCandGlyph;
        c.dsc.header.h      = kCandGlyph;
        c.dsc.header.stride = kCandGlyph;
        c.dsc.data_size     = static_cast<uint32_t>(kCandGlyph) * kCandGlyph;
        c.dsc.data          = c.buffer;

        c.image = lv_image_create(c.chip);
        lv_image_set_src(c.image, &c.dsc);
        lv_obj_set_style_image_recolor(c.image, lv_color_hex(kInk), 0);
        lv_obj_set_style_image_recolor_opa(c.image, LV_OPA_COVER, 0);
        lv_obj_align(c.image, LV_ALIGN_TOP_MID, 0, 2);
        // Taps land on the chip, not its children.
        lv_obj_clear_flag(c.image, LV_OBJ_FLAG_CLICKABLE);

        c.caption = lv_label_create(c.chip);
        lv_obj_set_style_text_font(c.caption, &lv_font_hanzi_ui_24, 0);
        lv_obj_set_style_text_color(c.caption, lv_color_hex(kGrey), 0);
        lv_obj_set_style_text_align(c.caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(c.caption, kCandChipW - 2);
        lv_obj_set_height(c.caption, 26);
        lv_label_set_long_mode(c.caption, LV_LABEL_LONG_DOT);
        lv_obj_align(c.caption, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_clear_flag(c.caption, LV_OBJ_FLAG_CLICKABLE);
    }

    // Grid positions follow the standard phone keypad: the letter groups sit
    // on keys 2..9 (abc top-middle through wxyz bottom-right), and the
    // letterless "1" slot at the top-left holds delete.
    for (uint8_t pos = 0; pos < 9; pos++) {
        const bool is_delete                   = pos == 0;
        const uint8_t group                    = static_cast<uint8_t>(pos - 1);
        lv_obj_t* key                          = lv_obj_create(_root);
        _keys[is_delete ? kLetterKeys : group] = key;
        lv_obj_remove_style_all(key);
        lv_obj_set_style_radius(key, 18, 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(kKeyBg), 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(kPressedBg), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(key, lv_color_hex(kReject), 0);
        lv_obj_set_style_border_width(key, 0, 0);
        lv_obj_set_size(key, kKeyW, kKeyH);
        lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
        // 50 px is ~4 mm of glass; the extended click area gives child
        // fingers the full key pitch.
        lv_obj_set_ext_click_area(key, 4);
        lv_obj_align(key, LV_ALIGN_CENTER, static_cast<int16_t>((pos % 3 - 1) * kKeyStepX), kKeyRowY[pos / 3]);
        // Press-state bookkeeping feeds keyActive(), which the mode-switch
        // host consults before acting on a horizontal swipe.
        lv_obj_add_event_cb(key, keyStateCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(key, keyStateCb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(key, keyStateCb, LV_EVENT_PRESS_LOST, this);
        if (!is_delete) {
            lv_obj_set_user_data(key, reinterpret_cast<void*>(static_cast<uintptr_t>(group)));
            lv_obj_add_event_cb(key, letterKeyCb, LV_EVENT_CLICKED, this);
            lv_obj_t* cap = lv_label_create(key);
            lv_obj_set_style_text_font(cap, &lv_font_hanzi_ui_24, 0);
            lv_obj_set_style_text_color(cap, lv_color_hex(kInk), 0);
            lv_label_set_text(cap, kKeyCaps[group]);
            lv_obj_center(cap);
            lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_event_cb(key, deleteShortCb, LV_EVENT_SHORT_CLICKED, this);
            lv_obj_add_event_cb(key, deleteLongCb, LV_EVENT_LONG_PRESSED, this);
            // The standard backspace glyph -- a left-pointing tab with an x
            // inside -- drawn with lines: no font glyph needed, and the
            // symbol every keyboard uses beats a bare arrow.
            // Sized to sit at the letter caps' visual weight (~24 px), not
            // to fill the key.
            static const lv_point_precise_t kBox[]    = {{31, 30}, {38, 23}, {52, 23}, {52, 37}, {38, 37}, {31, 30}};
            static const lv_point_precise_t kCross1[] = {{41, 27}, {47, 33}};
            static const lv_point_precise_t kCross2[] = {{47, 27}, {41, 33}};
            const struct {
                const lv_point_precise_t* pts;
                size_t count;
            } strokes[] = {
                {kBox, sizeof(kBox) / sizeof(kBox[0])},
                {kCross1, 2},
                {kCross2, 2},
            };
            for (const auto& s : strokes) {
                lv_obj_t* line = lv_line_create(key);
                lv_line_set_points(line, s.pts, static_cast<uint32_t>(s.count));
                lv_obj_set_style_line_width(line, 2, 0);
                lv_obj_set_style_line_color(line, lv_color_hex(kInk), 0);
                lv_obj_set_style_line_rounded(line, true, 0);
                lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }

    reset();
    return true;
}

void ImeView::destroy()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root       = nullptr;
        _hint_a     = nullptr;
        _hint_b     = nullptr;
        _page_label = nullptr;
        _empty_hint = nullptr;
        for (auto& key : _keys) {
            key = nullptr;
        }
        for (InterpChip& chip : _interp_chips) {
            chip.chip  = nullptr;
            chip.label = nullptr;
        }
        for (Cand& c : _cands) {
            c.chip    = nullptr;
            c.image   = nullptr;
            c.caption = nullptr;
        }
    }
    release();
    _source  = nullptr;
    _painter = nullptr;
}

void ImeView::setHidden(bool hidden)
{
    if (_root == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void ImeView::reset()
{
    _digits[0]    = '\0';
    _len          = 0;
    _interp_total = 0;
    _interp_base  = 0;
    _selected     = 0;
    _page         = 0;
    _cand_total   = 0;
    _pick_pending = false;
    refresh();
}

bool ImeView::takePick(uint16_t& id, char* reading, size_t cap)
{
    if (!_pick_pending) {
        return false;
    }
    _pick_pending = false;
    id            = _pick_id;
    if (reading != nullptr && cap > 0) {
        std::snprintf(reading, cap, "%s", _pick_reading);
    }
    return true;
}

void ImeView::handleKeyPressState(bool down)
{
    if (down) {
        _keys_down++;
    } else if (_keys_down > 0) {
        _keys_down--;
    }
}

void ImeView::exportState(char* prefix, size_t cap, int32_t& id) const
{
    // The letter string of the selected interpretation, never the digit
    // string: "43" is both "he" and "ge", the chip the child picked is not.
    std::snprintf(prefix, cap, "%s", _interp_total > 0 ? _interps[_selected] : "");
    id = -1;
}

void ImeView::importState(const char* prefix, int32_t id)
{
    if (_root == nullptr) {
        return;
    }
    _digits[0]     = '\0';
    _len           = 0;
    _interp_base   = 0;
    _selected      = 0;
    _page          = 0;
    _pick_pending  = false;
    const size_t n = prefix != nullptr ? std::strlen(prefix) : 0;
    if (n > 0 && n <= kMaxDigits) {
        char digits[kMaxDigits + 1] = {};
        bool mapped                 = true;
        for (size_t i = 0; i < n; i++) {
            digits[i] = pyDigitOf(prefix[i]);
            mapped    = mapped && digits[i] != 0;
        }
        // A legal letter prefix is one of its own digit string's
        // interpretations by construction; the scan pins the selection to
        // it (slot 0 stays as a data-bug fallback).
        const uint16_t total = mapped ? _source->interpretations(digits, _interps, T9Engine::kMaxInterps) : 0;
        if (total > 0) {
            std::memcpy(_digits, digits, sizeof(_digits));
            _len = static_cast<uint8_t>(n);
            for (uint16_t i = 0; i < total && i < T9Engine::kMaxInterps; i++) {
                if (std::strcmp(_interps[i], prefix) == 0) {
                    _selected = i;
                    break;
                }
            }
            // Keep the selected chip inside the visible window.
            _interp_base = _selected >= kInterpSlots - 1 ? _selected : 0;
            if (id >= 0) {
                // Page the strip to the carried character, so the selection
                // the child dialled in the other mode stays on screen here.
                uint16_t ids[16];
                const uint16_t count = _source->query(prefix, ids, 16, 0);
                for (uint16_t off = 0; off < count && _page == 0; off += 16) {
                    _source->query(prefix, ids, 16, off);
                    const uint16_t got = static_cast<uint16_t>(count - off < 16 ? count - off : 16);
                    for (uint16_t i = 0; i < got; i++) {
                        if (ids[i] == static_cast<uint16_t>(id)) {
                            _page = static_cast<uint16_t>((off + i) / kCandCells);
                            break;
                        }
                    }
                }
            }
        }
    }
    refresh();
}

void ImeView::handleLetterKey(uint8_t group)
{
    if (_root == nullptr || group >= kLetterKeys) {
        return;
    }
    if (_len >= kMaxDigits) {
        rejectCue(_keys[group]);
        return;
    }
    _digits[_len]     = static_cast<char>('2' + group);
    _digits[_len + 1] = '\0';
    // A key that leads to no interpretation is refused on the spot, so a
    // child can never enter an "empty results" state that needs undoing.
    if (_source->interpretations(_digits, nullptr, 0) == 0) {
        _digits[_len] = '\0';
        rejectCue(_keys[group]);
        return;
    }
    _len++;
    _interp_base = 0;
    _selected    = 0;
    _page        = 0;
    GetHAL().vibrate(15);
    refresh();
}

void ImeView::handleDeleteShort()
{
    if (_root == nullptr || _len == 0) {
        return;
    }
    _len--;
    _digits[_len] = '\0';
    _interp_base  = 0;
    _selected     = 0;
    _page         = 0;
    GetHAL().vibrate(15);
    refresh();
}

void ImeView::handleDeleteLong()
{
    if (_root == nullptr || _len == 0) {
        return;
    }
    GetHAL().vibrate(30);
    reset();
}

void ImeView::handleInterpChip(uint8_t slot)
{
    if (_root == nullptr || slot >= kInterpSlots || _interp_chips[slot].chip == nullptr ||
        lv_obj_has_flag(_interp_chips[slot].chip, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    GetHAL().vibrate(15);
    const InterpChip& chip = _interp_chips[slot];
    if (chip.more_dir > 0) {
        // Page forward by however many real chips the window held.
        uint16_t shown = 0;
        for (const InterpChip& c : _interp_chips) {
            if (c.more_dir == 0 && !lv_obj_has_flag(c.chip, LV_OBJ_FLAG_HIDDEN)) {
                shown++;
            }
        }
        _interp_base = static_cast<uint16_t>(_interp_base + shown);
        refreshInterps();
        return;
    }
    if (chip.more_dir < 0) {
        // Back to the front of the list (with at most six interpretations in
        // the real data there are only ever two windows).
        _interp_base = 0;
        refreshInterps();
        return;
    }
    if (chip.index >= _interp_total || chip.index == _selected) {
        return;
    }
    _selected = chip.index;
    _page     = 0;
    refreshInterps();
    refreshCandidates();
}

void ImeView::handleCandidate(uint8_t cell)
{
    if (_root == nullptr || cell >= kCandCells || !_cands[cell].occupied) {
        return;
    }
    GetHAL().vibrate(20);
    _pick_id = _cands[cell].id;
    matchedReading(_pick_id, _pick_reading, sizeof(_pick_reading));
    _pick_pending = true;
}

void ImeView::nextCandidatePage()
{
    const uint16_t pages = static_cast<uint16_t>((_cand_total + kCandCells - 1) / kCandCells);
    if (_page + 1 < pages) {
        _page++;
        refreshCandidates();
    }
}

void ImeView::previousCandidatePage()
{
    if (_page > 0) {
        _page--;
        refreshCandidates();
    }
}

void ImeView::rejectCue(lv_obj_t* key)
{
    GetHAL().vibrate(40);
    if (key == nullptr) {
        return;
    }
    lv_obj_set_style_border_width(key, 3, 0);
    lv_timer_t* timer = lv_timer_create(rejectTimerCb, 150, key);
    lv_timer_set_repeat_count(timer, 1);
}

void ImeView::matchedReading(uint16_t id, char* out, size_t cap) const
{
    out[0]              = '\0';
    const char* caption = _painter->caption(id);
    if (caption == nullptr || caption[0] == '\0') {
        return;
    }
    const char* interp         = (_interp_total > 0) ? _interps[_selected] : "";
    const size_t interp_len    = std::strlen(interp);
    const char* fallback_start = caption;
    size_t fallback_len        = 0;
    const char* p              = caption;
    while (*p != '\0') {
        const char* start = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        const size_t len = static_cast<size_t>(p - start);
        if (fallback_len == 0) {
            fallback_start = start;
            fallback_len   = len;
        }
        char token[16];
        if (len < sizeof(token)) {
            std::memcpy(token, start, len);
            token[len] = '\0';
            char plain[16];
            if (pyNormalize(token, plain, sizeof(plain)) > 0 && std::strncmp(plain, interp, interp_len) == 0) {
                std::snprintf(out, cap, "%s", token);
                return;
            }
        }
        while (*p == ' ') {
            p++;
        }
    }
    // No reading matches the interpretation (shouldn't happen for engine
    // results); fall back to the primary reading.
    const size_t n = fallback_len < cap - 1 ? fallback_len : cap - 1;
    std::memcpy(out, fallback_start, n);
    out[n] = '\0';
}

void ImeView::refreshInterps()
{
    _interp_total = _source->interpretations(_digits, _interps, T9Engine::kMaxInterps);
    if (_interp_total > T9Engine::kMaxInterps) {
        _interp_total = T9Engine::kMaxInterps;
    }
    if (_selected >= _interp_total) {
        _selected = 0;
    }
    if (_interp_base >= _interp_total) {
        _interp_base = 0;
    }

    // Double-ended window: a leading "..." appears whenever the window has
    // moved off the front (it pages back), a trailing one whenever more
    // interpretations follow (it pages forward).
    const bool has_back = _interp_base > 0;
    uint16_t avail      = static_cast<uint16_t>(kInterpSlots - (has_back ? 1 : 0));
    const bool has_fwd  = static_cast<uint16_t>(_interp_base + avail) < _interp_total;
    if (has_fwd) {
        avail--;
    }
    const uint16_t remaining = static_cast<uint16_t>(_interp_total - _interp_base);
    const uint16_t shown     = remaining < avail ? remaining : avail;

    // Measure, then centre the visible chips as one row.
    int16_t widths[kInterpSlots] = {};
    int16_t row_w                = 0;
    uint8_t visible              = 0;
    for (uint8_t i = 0; i < kInterpSlots; i++) {
        InterpChip& chip = _interp_chips[i];
        chip.more_dir    = 0;
        chip.index       = 0;
        const char* text = nullptr;
        if (has_back && i == 0) {
            text          = "...";
            chip.more_dir = -1;
        } else {
            const uint16_t offset = static_cast<uint16_t>(i - (has_back ? 1 : 0));
            if (offset < shown) {
                chip.index = static_cast<uint16_t>(_interp_base + offset);
                text       = _interps[chip.index];
            } else if (has_fwd && offset == shown) {
                text          = "...";
                chip.more_dir = 1;
            }
        }
        if (text == nullptr) {
            lv_obj_add_flag(chip.chip, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text(chip.label, text);
        lv_point_t size;
        lv_text_get_size(&size, text, &lv_font_pinyin_latin_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        widths[i] = static_cast<int16_t>(size.x + 2 * kChipPadX);
        row_w += widths[i] + (visible > 0 ? kChipGap : 0);
        visible++;
    }
    int16_t x = static_cast<int16_t>(-row_w / 2);
    for (uint8_t i = 0; i < kInterpSlots; i++) {
        InterpChip& chip = _interp_chips[i];
        if (widths[i] == 0) {
            continue;
        }
        lv_obj_clear_flag(chip.chip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(chip.chip, widths[i]);
        lv_obj_align(chip.chip, LV_ALIGN_CENTER, static_cast<int16_t>(x + widths[i] / 2), kChipY);
        x += widths[i] + kChipGap;

        const bool selected = chip.more_dir == 0 && chip.index == _selected;
        lv_obj_set_style_bg_opa(chip.chip, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(chip.label, lv_color_hex(selected ? kInk : kGrey), 0);
    }
}

void ImeView::refreshCandidates()
{
    const size_t glyph_px = static_cast<size_t>(kCandGlyph) * kCandGlyph;
    uint16_t ids[kCandCells];
    uint16_t got = 0;
    if (_interp_total > 0) {
        _cand_total = _source->query(_interps[_selected], ids, kCandCells, static_cast<uint16_t>(_page * kCandCells));
        if (_page > 0 && static_cast<uint32_t>(_page) * kCandCells >= _cand_total) {
            _page       = 0;
            _cand_total = _source->query(_interps[_selected], ids, kCandCells, 0);
        }
        const uint32_t remaining = _cand_total - static_cast<uint32_t>(_page) * kCandCells;
        got                      = static_cast<uint16_t>(remaining < kCandCells ? remaining : kCandCells);
    } else {
        _cand_total = 0;
    }

    for (uint8_t i = 0; i < kCandCells; i++) {
        Cand& c    = _cands[i];
        c.occupied = i < got;
        if (!c.occupied) {
            lv_obj_add_flag(c.chip, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        c.id = ids[i];
        lv_obj_clear_flag(c.chip, LV_OBJ_FLAG_HIDDEN);
        std::memset(c.buffer, 0, glyph_px);
        if (!_painter->paint(c.id, c.buffer, kCandGlyph, kCandGlyph)) {
            c.occupied = false;
            lv_obj_add_flag(c.chip, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char reading[16];
        matchedReading(c.id, reading, sizeof(reading));
        lv_label_set_text(c.caption, reading);
        // The buffer is reused in place, so the cached decoded image must go.
        lv_image_cache_drop(&c.dsc);
        lv_obj_invalidate(c.image);
    }

    const uint16_t pages = static_cast<uint16_t>((_cand_total + kCandCells - 1) / kCandCells);
    if (pages > 1) {
        char text[16];
        std::snprintf(text, sizeof(text), "%u/%u", _page + 1, pages);
        lv_label_set_text(_page_label, text);
        lv_obj_align(_page_label, LV_ALIGN_CENTER, 0, kPageY);
        lv_obj_clear_flag(_page_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_hint_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_hint_b, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_page_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_hint_a, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_hint_b, LV_OBJ_FLAG_HIDDEN);
    }
}

void ImeView::refresh()
{
    if (_root == nullptr) {
        return;
    }
    if (_len == 0) {
        lv_obj_clear_flag(_empty_hint, LV_OBJ_FLAG_HIDDEN);
        _interp_total = 0;
        for (InterpChip& chip : _interp_chips) {
            lv_obj_add_flag(chip.chip, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(_empty_hint, LV_OBJ_FLAG_HIDDEN);
        refreshInterps();
    }
    refreshCandidates();
}

namespace {

ImeView* viewOf(lv_event_t* e)
{
    return static_cast<ImeView*>(lv_event_get_user_data(e));
}

uint8_t indexOf(lv_event_t* e)
{
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    return static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)));
}

void letterKeyCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleLetterKey(indexOf(e));
    }
}

void deleteShortCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleDeleteShort();
    }
}

void deleteLongCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleDeleteLong();
    }
}

void interpChipCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleInterpChip(indexOf(e));
    }
}

void candidateCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleCandidate(indexOf(e));
    }
}

void keyStateCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleKeyPressState(lv_event_get_code(e) == LV_EVENT_PRESSED);
    }
}

}  // namespace

}  // namespace pime
