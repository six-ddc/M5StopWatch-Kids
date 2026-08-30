/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "picker_view.h"

#include <assets/assets.h>
#include <esp_heap_caps.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "py_normalize.h"

namespace pime {

namespace {

// Geometry (LV_ALIGN_CENTER offsets on the 466 px round screen, radius 233).
// The three wheels share one cylinder: rows sit at angle detent * kPitch /
// kWheelR, projected to y = R*sin, scaled by cos -- the same foreshortening
// an iOS picker fakes, and it naturally follows the round glass since the
// per-column visibility limit is the chord at that column's x extent.
constexpr int32_t kScreenC    = 233;
constexpr float kRadius       = 233.0f;
constexpr float kPitch        = 88.0f;   // detent spacing along the cylinder
constexpr float kWheelR       = 205.0f;  // cylinder radius
constexpr int16_t kBandW      = 460;
constexpr int16_t kBandH      = 112;
constexpr float kLift         = 0.0f;   // caption sits beside the glyph; no lift
constexpr int16_t kGap        = 20;     // gap between columns
constexpr int16_t kCaptionGap = 10;     // toned reading sits right of the glyph
constexpr int16_t kTextHalf   = 29;     // half extent of a 44 px row
constexpr float kAlphaMax     = 1.55f;  // rows past ~89 degrees never draw

// Scroll feel. Hand-tuned on the host sim; expect to retune on glass.
constexpr int32_t kTapSlop     = 8;       // px of movement that still counts as a tap
constexpr uint32_t kTapMs      = 400;     // press longer than this is not a tap
constexpr float kRubber        = 0.35f;   // overshoot compression past the ends
constexpr float kFlingMs       = 300.0f;  // projection horizon for release velocity
constexpr float kFlingMinV     = 0.05f;   // px/ms below which a release just snaps
constexpr uint32_t kSnapBase   = 220;     // ms
constexpr uint32_t kSnapPerRow = 45;      // ms per row of travel
constexpr uint32_t kSnapMax    = 850;     // ms
constexpr uint32_t kRevealMs   = 140;     // linked-rebuild fade-in
constexpr float kBounceRows    = 0.22f;   // key-step nudge at the ends

constexpr uint32_t kInk       = 0xF2F0EA;
constexpr uint32_t kGrey      = 0x8A8A88;
constexpr uint32_t kDim       = 0x464644;
constexpr uint32_t kFaint     = 0x2E2E2C;
constexpr uint32_t kBandBg    = 0x2C2B28;
constexpr uint32_t kBandFlash = 0x45443F;

// Fisheye ink by distance from the selection: full ink, mid, dim, faint --
// one ramp per wheel. The initial and suffix columns carry the classroom
// pinyin-card colour code (声母红、韵母蓝) in muted form, so the columns read
// as families without any separator; the character keeps pure ink, the
// brightest thing on screen.
constexpr uint32_t kRowInk[3][4] = {
    {0xE8B7B0, 0x8F7B77, 0x4A403E, 0x2F2A29},  // initials: muted coral
    {0xAFC4E0, 0x77828F, 0x3E434A, 0x292B2E},  // suffixes: muted periwinkle
    {kInk, kGrey, kDim, kFaint},               // characters: pure ink
};

const char* kEmptySuffixMark           = "\xC2\xB7";  // U+00B7 middle dot
constexpr const char* kDefaultSyllable = "hao";

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

lv_color_t rowColor(uint8_t wheel, float dist)
{
    const int di          = static_cast<int>(dist);
    const float frac      = dist - static_cast<float>(di);
    const lv_color_t near = lv_color_hex(kRowInk[wheel][std::min(di, 3)]);
    const lv_color_t far  = lv_color_hex(kRowInk[wheel][std::min(di + 1, 3)]);
    return lv_color_mix(far, near, static_cast<uint8_t>(frac * 255.0f));
}

void rootEventCb(lv_event_t* e);

}  // namespace

PickerView::~PickerView()
{
    destroy();
}

bool PickerView::allocate()
{
    const size_t glyph_px = static_cast<size_t>(kGlyphPx) * kGlyphPx;
    for (uint8_t i = 0; i < kSlots; i++) {
        _glyph_buf[i] = static_cast<uint8_t*>(heap_caps_malloc(glyph_px, MALLOC_CAP_SPIRAM));
        if (_glyph_buf[i] == nullptr) {
            return false;
        }
        std::memset(_glyph_buf[i], 0, glyph_px);
    }
    return true;
}

void PickerView::release()
{
    for (uint8_t i = 0; i < kSlots; i++) {
        if (_glyph_buf[i] != nullptr) {
            lv_image_cache_drop(&_glyph_dsc[i]);
            heap_caps_free(_glyph_buf[i]);
            _glyph_buf[i] = nullptr;
        }
    }
}

bool PickerView::create(lv_obj_t* parent, const CandidateSource* source, GlyphPainter* painter)
{
    if (source == nullptr || painter == nullptr || source->unitCount() == 0 || !allocate()) {
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
    // One gesture surface owns all three wheels: the rows are plain labels
    // and images, hit-testing picks the column from the press x.
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, rootEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(_root, rootEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_root, rootEventCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_root, rootEventCb, LV_EVENT_PRESS_LOST, this);

    _band = lv_obj_create(_root);
    lv_obj_remove_style_all(_band);
    lv_obj_set_size(_band, kBandW, kBandH);
    lv_obj_set_style_radius(_band, kBandH / 2, 0);
    lv_obj_set_style_bg_color(_band, lv_color_hex(kBandBg), 0);
    lv_obj_set_style_bg_opa(_band, LV_OPA_COVER, 0);
    lv_obj_align(_band, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(_band, LV_OBJ_FLAG_CLICKABLE);

    for (uint8_t w = 0; w < kWheelCount; w++) {
        Wheel& wh = _wheels[w];
        wh.owner  = this;
        wh.index  = w;
        for (uint8_t s = 0; s < kSlots; s++) {
            lv_obj_t* row;
            if (w == 2) {
                std::memset(&_glyph_dsc[s], 0, sizeof(_glyph_dsc[s]));
                _glyph_dsc[s].header.magic  = LV_IMAGE_HEADER_MAGIC;
                _glyph_dsc[s].header.cf     = LV_COLOR_FORMAT_A8;
                _glyph_dsc[s].header.w      = kGlyphPx;
                _glyph_dsc[s].header.h      = kGlyphPx;
                _glyph_dsc[s].header.stride = kGlyphPx;
                _glyph_dsc[s].data_size     = static_cast<uint32_t>(kGlyphPx) * kGlyphPx;
                _glyph_dsc[s].data          = _glyph_buf[s];
                row                         = lv_image_create(_root);
                lv_image_set_src(row, &_glyph_dsc[s]);
                lv_obj_set_style_image_recolor_opa(row, LV_OPA_COVER, 0);
            } else {
                row = lv_label_create(_root);
                lv_obj_set_style_text_font(row, &lv_font_hanzi_pinyin_44, 0);
            }
            lv_obj_set_style_transform_pivot_x(row, lv_pct(50), 0);
            lv_obj_set_style_transform_pivot_y(row, lv_pct(50), 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            wh.rows[s]    = row;
            wh.content[s] = -1;
            wh.text_w[s]  = 0;
        }
    }

    _caption = lv_label_create(_root);
    lv_obj_set_style_text_font(_caption, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_caption, lv_color_hex(kGrey), 0);
    lv_obj_clear_flag(_caption, LV_OBJ_FLAG_CLICKABLE);

    layout();

    if (!selectSyllable(kDefaultSyllable)) {
        // The default syllable is absent from this data set; land on the
        // first legal combination instead.
        _unit   = 0;
        _suffix = 0;
        composeSyllable();
        rebuildWheel(0, _source->unitCount(), 0.0f, false);
        rebuildWheel(1, _source->suffixCount(0), 0.0f, false);
        rebuildWheel(2, _source->queryExact(_syllable, nullptr, 0, 0), 0.0f, false);
        updateCaption();
    }
    return true;
}

void PickerView::layout()
{
    // Column budgets are measured, never estimated: the widest unit ("zh"),
    // the widest suffix ("uang") and the glyph box set the widths, and the
    // per-column visibility limit is the chord of the round glass at the
    // column's outer edge.
    lv_point_t sz;
    int32_t w_unit = 0;
    for (uint16_t u = 0; u < _source->unitCount(); u++) {
        lv_text_get_size(&sz, _source->unitAt(u), &lv_font_hanzi_pinyin_44, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        w_unit = std::max(w_unit, sz.x);
    }
    int32_t w_suffix = 0;
    for (uint16_t u = 0; u < _source->unitCount(); u++) {
        for (uint16_t s = 0; s < _source->suffixCount(u); s++) {
            const char* text = _source->suffixAt(u, s);
            lv_text_get_size(&sz, text[0] != '\0' ? text : kEmptySuffixMark, &lv_font_hanzi_pinyin_44, 0, 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            w_suffix = std::max(w_suffix, sz.x);
        }
    }
    // The character column is the widest of the three by design: it holds
    // the payoff of the whole exercise.
    const int32_t w_char = std::max<int32_t>(kGlyphPx + 24, w_suffix + 8);

    const int32_t total = w_unit + w_suffix + w_char + 2 * kGap;
    const int32_t left  = -total / 2;
    _wheels[0].x        = static_cast<int16_t>(left + w_unit / 2);
    _wheels[0].width    = static_cast<int16_t>(w_unit);
    _wheels[1].x        = static_cast<int16_t>(left + w_unit + kGap + w_suffix / 2);
    _wheels[1].width    = static_cast<int16_t>(w_suffix);
    _wheels[2].x        = static_cast<int16_t>(left + w_unit + kGap + w_suffix + kGap + w_char / 2);
    _wheels[2].width    = static_cast<int16_t>(w_char);
    for (Wheel& wh : _wheels) {
        const float edge = static_cast<float>(std::abs(wh.x)) + static_cast<float>(wh.width) / 2.0f + 2.0f;
        wh.y_lim         = edge < kRadius ? static_cast<int16_t>(std::sqrt(kRadius * kRadius - edge * edge)) : 0;
    }

    // Caption x depends on its text width; updateCaption() places it.
}

void PickerView::destroy()
{
    if (_root != nullptr) {
        for (Wheel& wh : _wheels) {
            lv_anim_delete(&wh, nullptr);
        }
        lv_anim_delete(_band, nullptr);
        lv_obj_delete(_root);
        _root    = nullptr;
        _band    = nullptr;
        _caption = nullptr;
        for (Wheel& wh : _wheels) {
            for (auto& row : wh.rows) {
                row = nullptr;
            }
            wh = Wheel{};
        }
    }
    release();
    _source       = nullptr;
    _painter      = nullptr;
    _drag_wheel   = -1;
    _pick_pending = false;
}

void PickerView::setHidden(bool hidden)
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

bool PickerView::takePick(uint16_t& id, char* reading, size_t cap)
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

// ---------------------------------------------------------------------------
// selection state

void PickerView::composeSyllable()
{
    std::snprintf(_syllable, sizeof(_syllable), "%s%s", _source->unitAt(_unit), _source->suffixAt(_unit, _suffix));
}

uint16_t PickerView::candidateAt(uint16_t index) const
{
    uint16_t id = 0;
    _source->queryExact(_syllable, &id, 1, index);
    return id;
}

uint16_t PickerView::candidateIndex() const
{
    const Wheel& wh = _wheels[2];
    if (wh.count == 0) {
        return 0;
    }
    const int32_t det = static_cast<int32_t>(std::lround(wh.offset));
    return static_cast<uint16_t>(std::clamp<int32_t>(det, 0, wh.count - 1));
}

float PickerView::wheelOffset(uint8_t wheel) const
{
    return wheel < kWheelCount ? _wheels[wheel].offset : 0.0f;
}

int16_t PickerView::wheelX(uint8_t wheel) const
{
    return wheel < kWheelCount ? _wheels[wheel].x : 0;
}

bool PickerView::wheelSettled() const
{
    for (const Wheel& wh : _wheels) {
        if (wh.snapping || wh.bouncing) {
            return false;
        }
    }
    return _drag_wheel < 0;
}

bool PickerView::selectSyllable(const char* syl)
{
    uint16_t unit = 0, suffix = 0;
    if (_root == nullptr || syl == nullptr || !_source->locate(syl, unit, suffix)) {
        return false;
    }
    _unit   = unit;
    _suffix = suffix;
    composeSyllable();
    rebuildWheel(0, _source->unitCount(), static_cast<float>(unit), false);
    rebuildWheel(1, _source->suffixCount(unit), static_cast<float>(suffix), false);
    rebuildWheel(2, _source->queryExact(_syllable, nullptr, 0, 0), 0.0f, false);
    updateCaption();
    return true;
}

void PickerView::showCharacter(uint16_t id)
{
    if (_root == nullptr) {
        return;
    }
    // Primary reading -> toneless syllable -> wheel positions; any failure
    // falls back to the default syllable, which create() already dialled.
    const char* caption = _painter->caption(id);
    char token[16]      = {};
    if (caption != nullptr) {
        size_t n = 0;
        while (caption[n] != '\0' && caption[n] != ' ' && n < sizeof(token) - 1) {
            token[n] = caption[n];
            n++;
        }
        token[n] = '\0';
    }
    char plain[16];
    if (pyNormalize(token, plain, sizeof(plain)) == 0 || !selectSyllable(plain)) {
        selectSyllable(kDefaultSyllable);
        return;
    }
    // Scroll the character wheel to this exact character.
    const uint16_t total = _wheels[2].count;
    uint16_t ids[16];
    for (uint16_t off = 0; off < total; off += 16) {
        _source->queryExact(_syllable, ids, 16, off);
        const uint16_t got = std::min<uint16_t>(16, total - off);
        for (uint16_t i = 0; i < got; i++) {
            if (ids[i] == id) {
                _wheels[2].offset = static_cast<float>(off + i);
                _wheels[2].detent = off + i;
                refreshWheel(2);
                updateCaption();
                return;
            }
        }
    }
}

void PickerView::setUnit(uint16_t unit)
{
    if (unit == _unit || unit >= _source->unitCount()) {
        return;
    }
    // Keep the suffix if the new unit still has it; otherwise snap to the
    // nearest in letter order (the first suffix sorting >= the old one).
    char kept[kMaxSyllable + 1];
    std::snprintf(kept, sizeof(kept), "%s", _source->suffixAt(_unit, _suffix));
    _unit                = unit;
    const uint16_t count = _source->suffixCount(unit);
    uint16_t chosen      = count > 0 ? static_cast<uint16_t>(count - 1) : 0;
    for (uint16_t s = 0; s < count; s++) {
        if (std::strcmp(_source->suffixAt(unit, s), kept) >= 0) {
            chosen = s;
            break;
        }
    }
    _suffix = chosen;
    composeSyllable();
    rebuildWheel(1, count, static_cast<float>(chosen), true);
    rebuildWheel(2, _source->queryExact(_syllable, nullptr, 0, 0), 0.0f, true);
    updateCaption();
}

void PickerView::setSuffix(uint16_t suffix)
{
    if (suffix == _suffix || suffix >= _source->suffixCount(_unit)) {
        return;
    }
    _suffix = suffix;
    composeSyllable();
    rebuildWheel(2, _source->queryExact(_syllable, nullptr, 0, 0), 0.0f, true);
    updateCaption();
}

// ---------------------------------------------------------------------------
// rendering

void PickerView::matchedReading(uint16_t id, char* out, size_t cap) const
{
    out[0]              = '\0';
    const char* caption = _painter->caption(id);
    if (caption == nullptr || caption[0] == '\0') {
        return;
    }
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
            if (pyNormalize(token, plain, sizeof(plain)) > 0 && std::strcmp(plain, _syllable) == 0) {
                std::snprintf(out, cap, "%s", token);
                return;
            }
        }
        while (*p == ' ') {
            p++;
        }
    }
    const size_t n = fallback_len < cap - 1 ? fallback_len : cap - 1;
    std::memcpy(out, fallback_start, n);
    out[n] = '\0';
}

void PickerView::updateCaption()
{
    if (_wheels[2].count == 0) {
        lv_obj_add_flag(_caption, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char reading[16];
    matchedReading(candidateAt(candidateIndex()), reading, sizeof(reading));
    lv_label_set_text(_caption, reading);
    // Beside the glyph, on the band's one horizontal axis: the whole
    // blending line (initial | suffix | glyph reading) shares a baseline.
    lv_point_t size;
    lv_text_get_size(&size, reading, &lv_font_hanzi_ui_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t x = _wheels[2].x + kGlyphPx / 2 + kCaptionGap + size.x / 2;
    lv_obj_align(_caption, LV_ALIGN_CENTER, x, 0);
    lv_obj_clear_flag(_caption, LV_OBJ_FLAG_HIDDEN);
}

void PickerView::assignRow(Wheel& wh, uint8_t slot, int32_t item)
{
    wh.content[slot] = item;
    lv_obj_t* row    = wh.rows[slot];
    if (wh.index == 2) {
        const uint16_t id     = candidateAt(static_cast<uint16_t>(item));
        const size_t glyph_px = static_cast<size_t>(kGlyphPx) * kGlyphPx;
        std::memset(_glyph_buf[slot], 0, glyph_px);
        _painter->paint(id, _glyph_buf[slot], kGlyphPx, kGlyphPx);
        // The buffer is reused in place, so the cached decoded image must go.
        lv_image_cache_drop(&_glyph_dsc[slot]);
        lv_obj_invalidate(row);
        wh.text_w[slot] = kGlyphPx;
        return;
    }
    const char* text = wh.index == 0 ? _source->unitAt(static_cast<uint16_t>(item))
                                     : _source->suffixAt(_unit, static_cast<uint16_t>(item));
    if (text[0] == '\0') {
        text = kEmptySuffixMark;
    }
    lv_label_set_text(row, text);
    lv_point_t sz;
    lv_text_get_size(&sz, text, &lv_font_hanzi_pinyin_44, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    wh.text_w[slot] = sz.x;
}

float PickerView::rubberOffset(const Wheel& wh) const
{
    const float max = static_cast<float>(wh.count > 0 ? wh.count - 1 : 0);
    if (wh.offset < 0.0f) {
        return wh.offset * kRubber;
    }
    if (wh.offset > max) {
        return max + (wh.offset - max) * kRubber;
    }
    return wh.offset;
}

void PickerView::refreshWheel(uint8_t w)
{
    if (_root == nullptr) {
        return;
    }
    Wheel& wh        = _wheels[w];
    const float disp = rubberOffset(wh);
    const int32_t lo = static_cast<int32_t>(std::floor(disp)) - (kSlots / 2);
    for (int32_t item = lo; item < lo + kSlots; item++) {
        const uint8_t slot = static_cast<uint8_t>(((item % kSlots) + kSlots) % kSlots);
        lv_obj_t* row      = wh.rows[slot];
        if (item < 0 || item >= wh.count) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const float d     = static_cast<float>(item) - disp;
        const float alpha = d * kPitch / kWheelR;
        if (alpha <= -kAlphaMax || alpha >= kAlphaMax) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (wh.content[slot] != item) {
            assignRow(wh, slot, item);
        }
        const float dist  = std::fabs(d);
        const float scale = std::cos(alpha);
        const float y     = kWheelR * std::sin(alpha) + kLift * std::max(0.0f, 1.0f - dist);
        const float half  = (w == 2 ? kGlyphPx / 2.0f : static_cast<float>(kTextHalf)) * scale;
        const float room  = static_cast<float>(wh.y_lim) - std::fabs(y) - half;
        if (room <= 0.0f) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        // Wide content never leaves its column: the selected size is scaled
        // down to the measured budget (iang/uang keep their full height
        // rhythm, just narrower ink).
        const float fit = wh.text_w[slot] > wh.width ? static_cast<float>(wh.width) / wh.text_w[slot] : 1.0f;
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(row, LV_ALIGN_CENTER, wh.x, static_cast<int32_t>(std::lround(y)));
        lv_obj_set_style_transform_scale(row, static_cast<int32_t>(256.0f * scale * fit), 0);
        const lv_color_t ink = rowColor(w, dist);
        if (w == 2) {
            lv_obj_set_style_image_recolor(row, ink, 0);
        } else {
            lv_obj_set_style_text_color(row, ink, 0);
        }
        const float edge = clampf(room / 44.0f, 0.0f, 1.0f);
        lv_obj_set_style_opa(row, static_cast<lv_opa_t>(255.0f * edge * wh.reveal), 0);
    }

    if (w == 2) {
        // The toned reading belongs to the settled state: it melts away
        // while the wheel is between detents and returns as it locks.
        const float away = std::fabs(disp - std::lround(disp));
        const float lock = clampf(1.0f - 2.0f * away, 0.0f, 1.0f);
        lv_obj_set_style_opa(_caption, static_cast<lv_opa_t>(255.0f * lock * wh.reveal), 0);
    }
}

void PickerView::rebuildWheel(uint8_t w, uint16_t count, float offset, bool animate)
{
    Wheel& wh = _wheels[w];
    stopWheelAnims(w);
    wh.snapping = false;
    wh.bouncing = false;
    wh.count    = count;
    wh.offset   = offset;
    wh.detent   = static_cast<int32_t>(std::lround(offset));
    for (auto& content : wh.content) {
        content = -1;  // force reassignment: the item lists changed
    }
    if (animate) {
        // Linked rebuilds ease in rather than popping: a short fade, driven
        // through the same per-row opacity math the fisheye uses.
        wh.reveal = 0.25f;
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, &wh);
        lv_anim_set_exec_cb(&anim, revealAnimCb);
        lv_anim_set_values(&anim, 256, 1024);
        lv_anim_set_duration(&anim, kRevealMs);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    } else {
        wh.reveal = 1.0f;
    }
    refreshWheel(w);
}

// ---------------------------------------------------------------------------
// scrolling

void PickerView::tickCrossings(Wheel& wh)
{
    if (wh.count == 0) {
        return;
    }
    const int32_t det =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0, static_cast<int32_t>(wh.count) - 1);
    if (det != wh.detent) {
        wh.detent = det;
        GetHAL().vibrate(5);
        if (wh.index == 2) {
            updateCaption();
        }
    }
}

void PickerView::stopWheelAnims(uint8_t w)
{
    lv_anim_delete(&_wheels[w], offsetAnimCb);
}

void PickerView::startOffsetAnim(uint8_t w, float target, uint32_t duration_ms)
{
    Wheel& wh = _wheels[w];
    stopWheelAnims(w);
    if (std::fabs(target - wh.offset) < 0.001f) {
        wh.offset = target;
        refreshWheel(w);
        settleWheel(w, false);
        return;
    }
    wh.snapping = true;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, &wh);
    lv_anim_set_exec_cb(&anim, offsetAnimCb);
    lv_anim_set_values(&anim, static_cast<int32_t>(wh.offset * 1024.0f), static_cast<int32_t>(target * 1024.0f));
    lv_anim_set_duration(&anim, duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, offsetAnimDoneCb);
    lv_anim_start(&anim);
}

void PickerView::settleWheel(uint8_t w, bool from_motion)
{
    Wheel& wh = _wheels[w];
    if (wh.count == 0) {
        return;
    }
    const int32_t det =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0, static_cast<int32_t>(wh.count) - 1);
    wh.offset = static_cast<float>(det);
    wh.detent = det;
    refreshWheel(w);
    if (from_motion && det != wh.travel_from) {
        GetHAL().vibrate(10);
    }
    if (w == 0) {
        setUnit(static_cast<uint16_t>(det));
    } else if (w == 1) {
        setSuffix(static_cast<uint16_t>(det));
    } else {
        updateCaption();
    }
}

void PickerView::scrollToRow(uint8_t w, int32_t row, bool /*from_key*/)
{
    Wheel& wh = _wheels[w];
    if (wh.count == 0) {
        return;
    }
    row = std::clamp<int32_t>(row, 0, static_cast<int32_t>(wh.count) - 1);
    wh.travel_from =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0, static_cast<int32_t>(wh.count) - 1);
    const float distance    = std::fabs(static_cast<float>(row) - wh.offset);
    const uint32_t duration = std::min<uint32_t>(kSnapBase + static_cast<uint32_t>(kSnapPerRow * distance), kSnapMax);
    startOffsetAnim(w, static_cast<float>(row), duration);
}

void PickerView::candidateStep(int8_t dir)
{
    Wheel& wh = _wheels[2];
    if (_root == nullptr || wh.count == 0) {
        return;
    }
    stopWheelAnims(2);
    const int32_t cur =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0, static_cast<int32_t>(wh.count) - 1);
    const int32_t target = cur + dir;
    if (target < 0 || target >= wh.count) {
        // The end of the list answers with a nudge-and-return, not silence.
        wh.offset      = static_cast<float>(cur);
        wh.bouncing    = true;
        wh.bounce_home = static_cast<float>(cur);
        wh.snapping    = true;
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, &wh);
        lv_anim_set_exec_cb(&anim, offsetAnimCb);
        lv_anim_set_values(&anim, static_cast<int32_t>(wh.offset * 1024.0f),
                           static_cast<int32_t>((wh.offset + dir * kBounceRows) * 1024.0f));
        lv_anim_set_duration(&anim, 90);
        lv_anim_set_completed_cb(&anim, offsetAnimDoneCb);
        lv_anim_start(&anim);
        return;
    }
    scrollToRow(2, target, true);
}

// ---------------------------------------------------------------------------
// input

int8_t PickerView::wheelForPoint(int32_t x) const
{
    const int32_t rel = x - kScreenC;
    for (uint8_t w = 0; w < kWheelCount; w++) {
        const int32_t half = _wheels[w].width / 2 + kGap / 2 - 1;
        if (rel >= _wheels[w].x - half && rel <= _wheels[w].x + half) {
            return static_cast<int8_t>(w);
        }
    }
    return -1;
}

void PickerView::handlePress(int32_t x, int32_t y)
{
    _drag_wheel    = wheelForPoint(x);
    _drag_steering = false;
    _press_x       = x;
    _press_y       = y;
    _press_ms      = lv_tick_get();
    _last_y        = y;
    _last_ms       = _press_ms;
    _velocity      = 0.0f;
    if (_drag_wheel >= 0) {
        Wheel& wh = _wheels[_drag_wheel];
        // Catching a spinning wheel stops it where it is; the release will
        // snap it onto a detent again.
        stopWheelAnims(_drag_wheel);
        wh.snapping    = false;
        wh.bouncing    = false;
        _press_offset  = wh.offset;
        wh.travel_from = std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0,
                                             static_cast<int32_t>(wh.count > 0 ? wh.count - 1 : 0));
    }
}

void PickerView::handleMove(int32_t x, int32_t y)
{
    if (_drag_wheel < 0) {
        return;
    }
    const int32_t dx = x - _press_x;
    const int32_t dy = y - _press_y;
    if (!_drag_steering) {
        // Steering starts on a mostly-vertical move past the tap slop; a
        // horizontal move stays available to the host's own gestures.
        if (std::abs(dy) > kTapSlop && std::abs(dy) >= std::abs(dx)) {
            _drag_steering = true;
        } else if (std::abs(dx) > kTapSlop * 2) {
            _drag_wheel = -1;
            return;
        }
    }
    const uint32_t now = lv_tick_get();
    if (now > _last_ms) {
        const float inst = static_cast<float>(y - _last_y) / static_cast<float>(now - _last_ms);
        _velocity        = 0.75f * _velocity + 0.25f * inst;
        _last_y          = y;
        _last_ms         = now;
    }
    if (_drag_steering) {
        Wheel& wh = _wheels[_drag_wheel];
        wh.offset = _press_offset + static_cast<float>(_press_y - y) / kPitch;
        tickCrossings(wh);
        refreshWheel(static_cast<uint8_t>(_drag_wheel));
    }
}

void PickerView::handleRelease(int32_t x, int32_t y)
{
    const int8_t w     = _drag_wheel;
    _drag_wheel        = -1;
    const uint32_t now = lv_tick_get();
    const bool tap = std::abs(x - _press_x) < kTapSlop && std::abs(y - _press_y) < kTapSlop && now - _press_ms < kTapMs;

    if (tap) {
        const int32_t y_rel = _press_y - kScreenC;
        if (std::abs(y_rel) <= kBandH / 2) {
            // A tap on the selection band confirms the dialled character.
            confirmPick();
            return;
        }
        if (w >= 0) {
            // A tap on any other row scrolls that row into the band.
            const float sin_a = clampf(static_cast<float>(y_rel) / kWheelR, -1.0f, 1.0f);
            const float d     = std::asin(sin_a) * kWheelR / kPitch;
            const int32_t row = static_cast<int32_t>(std::lround(_wheels[w].offset + d));
            if (row >= 0 && row < _wheels[w].count && row != _wheels[w].detent) {
                scrollToRow(static_cast<uint8_t>(w), row, false);
            }
        }
        return;
    }
    if (w < 0) {
        return;
    }
    Wheel& wh = _wheels[w];
    if (!_drag_steering) {
        return;
    }
    // Fling: project the release velocity over a fixed horizon, land on the
    // nearest detent of the projected position. A stale sample means the
    // finger paused before lifting -- no inertia then.
    float v = _velocity;
    if (now - _last_ms > 80 || std::fabs(v) < kFlingMinV) {
        v = 0.0f;
    }
    const float projected = wh.offset - v * kFlingMs / kPitch;
    const int32_t target =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(projected)), 0, static_cast<int32_t>(wh.count) - 1);
    const float distance    = std::fabs(static_cast<float>(target) - wh.offset);
    const uint32_t duration = std::min<uint32_t>(kSnapBase + static_cast<uint32_t>(kSnapPerRow * distance), kSnapMax);
    startOffsetAnim(static_cast<uint8_t>(w), static_cast<float>(target), duration);
}

void PickerView::handleAbort()
{
    const int8_t w = _drag_wheel;
    _drag_wheel    = -1;
    if (w < 0) {
        return;
    }
    Wheel& wh = _wheels[w];
    if (wh.count == 0) {
        return;
    }
    const int32_t det =
        std::clamp<int32_t>(static_cast<int32_t>(std::lround(wh.offset)), 0, static_cast<int32_t>(wh.count) - 1);
    startOffsetAnim(static_cast<uint8_t>(w), static_cast<float>(det), kSnapBase);
}

void PickerView::confirmPick()
{
    if (_wheels[2].count == 0) {
        return;
    }
    GetHAL().vibrate(20);
    _pick_id = candidateAt(candidateIndex());
    matchedReading(_pick_id, _pick_reading, sizeof(_pick_reading));
    _pick_pending = true;
    // A short flash of the band acknowledges the pick before the page
    // changes underneath it.
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, _band);
    lv_anim_set_exec_cb(&anim, flashAnimCb);
    lv_anim_set_values(&anim, 255, 0);
    lv_anim_set_duration(&anim, 240);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

// ---------------------------------------------------------------------------
// static callbacks

void PickerView::offsetAnimCb(void* var, int32_t v)
{
    Wheel* wh  = static_cast<Wheel*>(var);
    wh->offset = static_cast<float>(v) / 1024.0f;
    wh->owner->tickCrossings(*wh);
    wh->owner->refreshWheel(wh->index);
}

void PickerView::offsetAnimDoneCb(lv_anim_t* anim)
{
    Wheel* wh = static_cast<Wheel*>(anim->var);
    if (wh->bouncing) {
        wh->bouncing = false;
        wh->owner->startOffsetAnim(wh->index, wh->bounce_home, 160);
        return;
    }
    wh->snapping = false;
    wh->owner->settleWheel(wh->index, true);
}

void PickerView::revealAnimCb(void* var, int32_t v)
{
    Wheel* wh  = static_cast<Wheel*>(var);
    wh->reveal = static_cast<float>(v) / 1024.0f;
    wh->owner->refreshWheel(wh->index);
}

void PickerView::flashAnimCb(void* var, int32_t v)
{
    lv_obj_t* band = static_cast<lv_obj_t*>(var);
    lv_obj_set_style_bg_color(
        band, lv_color_mix(lv_color_hex(kBandFlash), lv_color_hex(kBandBg), static_cast<uint8_t>(v)), 0);
}

namespace {

void rootEventCb(lv_event_t* e)
{
    auto* view = static_cast<PickerView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESS_LOST) {
        view->handleAbort();
        return;
    }
    lv_indev_t* dev = lv_indev_active();
    if (dev == nullptr) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(dev, &p);
    if (code == LV_EVENT_PRESSED) {
        view->handlePress(p.x, p.y);
    } else if (code == LV_EVENT_PRESSING) {
        view->handleMove(p.x, p.y);
    } else if (code == LV_EVENT_RELEASED) {
        view->handleRelease(p.x, p.y);
    }
}

}  // namespace

}  // namespace pime
