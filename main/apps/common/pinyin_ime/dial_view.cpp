/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial_view.h"

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
// The 26 letters sit on one ring, 'a' at 12 o'clock running clockwise; the
// centre disc holds the echo, a 5+4 honeycomb of candidates and the page
// indicator. There are no A/B guide labels on this page -- the ring owns the
// whole rim -- which is a deliberate exception to the app-wide "guide under
// its key" rule; A/B page the candidate grid as everywhere else.
constexpr int16_t kScreenC   = 233;
constexpr float kRingR       = 203.0f;
constexpr float kSlotDeg     = 360.0f / 26.0f;
constexpr float kRingHitMin  = 170.0f;  // annulus that counts as a ring touch
constexpr float kRingHitMax  = 245.0f;  // beyond the glass never happens
constexpr float kSnapSlots   = 2.0f;    // max slots a touch may snap across
constexpr int16_t kEchoY     = -112;
constexpr int16_t kEmptyY    = -20;
constexpr int16_t kRowY[2]   = {-26, 58};
constexpr int16_t kCandPitch = 64;
constexpr int16_t kCandW     = 58;
constexpr int16_t kCandH     = 80;
constexpr int16_t kPageY     = 112;
constexpr float kDotR        = 179.0f;  // recall dot, inside its letter

// Magnifier: armed letter scales up and floats toward the centre.
// Hand-tuned on the host sim; expect to retune on glass.
constexpr int32_t kMagnifyScale = 400;  // 256 = 1x
constexpr int16_t kMagnifyLift  = 30;   // px toward the centre

constexpr uint32_t kInk       = 0xF2F0EA;
constexpr uint32_t kGrey      = 0x8A8A88;
constexpr uint32_t kDimmed    = 0x464644;
constexpr uint32_t kCandBg    = 0x262624;
constexpr uint32_t kPressedBg = 0x45443F;

float slotAngleRad(uint8_t slot)
{
    return (-90.0f + slot * kSlotDeg) * static_cast<float>(M_PI) / 180.0f;
}

void ringEventCb(lv_event_t* e);
void echoShortCb(lv_event_t* e);
void echoLongCb(lv_event_t* e);
void candidateCb(lv_event_t* e);
void flyBackDeleteCb(lv_anim_t* anim);

}  // namespace

DialView::~DialView()
{
    destroy();
}

bool DialView::allocate()
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

void DialView::release()
{
    for (Cand& c : _cands) {
        if (c.buffer != nullptr) {
            lv_image_cache_drop(&c.dsc);
            heap_caps_free(c.buffer);
            c.buffer = nullptr;
        }
    }
}

bool DialView::create(lv_obj_t* parent, const T9Engine* source, GlyphPainter* painter)
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
    // One gesture surface handles the whole ring band: the letters are plain
    // labels, hit-testing is done by angle so a touch can snap to the
    // nearest bright letter.
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, ringEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(_root, ringEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_root, ringEventCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_root, ringEventCb, LV_EVENT_PRESS_LOST, this);

    for (uint8_t i = 0; i < kLetters; i++) {
        lv_obj_t* label = lv_label_create(_root);
        _letters[i]     = label;
        lv_obj_set_style_text_font(label, &lv_font_pinyin_latin_32, 0);
        char text[2] = {static_cast<char>('a' + i), '\0'};
        lv_label_set_text(label, text);
        const float a = slotAngleRad(i);
        lv_obj_align(label, LV_ALIGN_CENTER, static_cast<int16_t>(kRingR * std::cos(a)),
                     static_cast<int16_t>(kRingR * std::sin(a)));
        lv_obj_set_style_transform_pivot_x(label, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(label, lv_pct(50), 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    }

    _recall_dot = lv_obj_create(_root);
    lv_obj_remove_style_all(_recall_dot);
    lv_obj_set_style_radius(_recall_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_recall_dot, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(_recall_dot, LV_OPA_COVER, 0);
    lv_obj_set_size(_recall_dot, 6, 6);
    lv_obj_clear_flag(_recall_dot, LV_OBJ_FLAG_CLICKABLE);

    _echo = lv_label_create(_root);
    lv_obj_set_style_text_font(_echo, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_echo, lv_color_hex(kInk), 0);
    lv_obj_align(_echo, LV_ALIGN_CENTER, 0, kEchoY);
    lv_obj_add_flag(_echo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(_echo, 20);
    lv_obj_add_event_cb(_echo, echoShortCb, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(_echo, echoLongCb, LV_EVENT_LONG_PRESSED, this);

    _pending = lv_label_create(_root);
    lv_obj_set_style_text_font(_pending, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_pending, lv_color_hex(kGrey), 0);
    lv_obj_clear_flag(_pending, LV_OBJ_FLAG_CLICKABLE);

    _empty_hint = lv_label_create(_root);
    lv_obj_set_style_text_font(_empty_hint, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_empty_hint, lv_color_hex(kGrey), 0);
    lv_label_set_text(_empty_hint, "想学哪个字?");
    lv_obj_align(_empty_hint, LV_ALIGN_CENTER, 0, kEmptyY);
    lv_obj_clear_flag(_empty_hint, LV_OBJ_FLAG_CLICKABLE);

    _page_label = lv_label_create(_root);
    lv_obj_set_style_text_font(_page_label, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_page_label, lv_color_hex(kGrey), 0);
    lv_obj_align(_page_label, LV_ALIGN_CENTER, 0, kPageY);
    lv_obj_clear_flag(_page_label, LV_OBJ_FLAG_CLICKABLE);

    for (uint8_t i = 0; i < kCandCells; i++) {
        Cand& c = _cands[i];
        c.chip  = lv_obj_create(_root);
        lv_obj_remove_style_all(c.chip);
        lv_obj_set_style_radius(c.chip, 10, 0);
        lv_obj_set_style_bg_color(c.chip, lv_color_hex(kCandBg), 0);
        lv_obj_set_style_bg_color(c.chip, lv_color_hex(kPressedBg), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(c.chip, LV_OPA_COVER, 0);
        lv_obj_set_size(c.chip, kCandW, kCandH);
        lv_obj_clear_flag(c.chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c.chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c.chip, candidateCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(c.chip, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        const uint8_t row  = i < kRow0 ? 0 : 1;
        const uint8_t col  = row == 0 ? i : static_cast<uint8_t>(i - kRow0);
        const uint8_t cols = row == 0 ? kRow0 : kCandCells - kRow0;
        lv_obj_align(c.chip, LV_ALIGN_CENTER, static_cast<int16_t>((col - (cols - 1) * 0.5f) * kCandPitch), kRowY[row]);

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
        lv_obj_clear_flag(c.image, LV_OBJ_FLAG_CLICKABLE);

        c.caption = lv_label_create(c.chip);
        lv_obj_set_style_text_font(c.caption, &lv_font_hanzi_ui_24, 0);
        lv_obj_set_style_text_color(c.caption, lv_color_hex(kGrey), 0);
        lv_obj_set_style_text_align(c.caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(c.caption, kCandW - 2);
        lv_label_set_long_mode(c.caption, LV_LABEL_LONG_DOT);
        lv_obj_align(c.caption, LV_ALIGN_BOTTOM_MID, 0, -1);
        lv_obj_clear_flag(c.caption, LV_OBJ_FLAG_CLICKABLE);
    }

    reset();
    return true;
}

void DialView::destroy()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root       = nullptr;
        _echo       = nullptr;
        _pending    = nullptr;
        _empty_hint = nullptr;
        _page_label = nullptr;
        _recall_dot = nullptr;
        for (auto& l : _letters) {
            l = nullptr;
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

void DialView::setHidden(bool hidden)
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

void DialView::reset()
{
    _prefix[0]    = '\0';
    _len          = 0;
    _armed        = -1;
    _page         = 0;
    _cand_total   = 0;
    _pick_pending = false;
    refresh();
}

bool DialView::takePick(uint16_t& id, char* reading, size_t cap)
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

void DialView::exportState(char* prefix, size_t cap, int32_t& id) const
{
    std::snprintf(prefix, cap, "%s", _prefix);
    // The grid shows candidates but never singles one out, so no id rides
    // along; the receiving mode keeps only the prefix.
    id = -1;
}

void DialView::importState(const char* prefix, int32_t id)
{
    if (_root == nullptr) {
        return;
    }
    armSlot(-1);
    _prefix[0]     = '\0';
    _len           = 0;
    _page          = 0;
    _pick_pending  = false;
    const size_t n = prefix != nullptr ? std::strlen(prefix) : 0;
    // The carrier comes from a sibling input mode, so it is legal by
    // construction; the query guard only catches data bugs.
    if (n > 0 && n <= kMaxLen && _source->query(prefix, nullptr, 0, 0) > 0) {
        std::memcpy(_prefix, prefix, n + 1);
        _len = static_cast<uint8_t>(n);
        if (id >= 0) {
            // Page the grid to the carried character, so the selection the
            // child dialled in the other mode stays on screen here.
            uint16_t ids[16];
            const uint16_t total = _source->query(_prefix, ids, 16, 0);
            for (uint16_t off = 0; off < total && _page == 0; off += 16) {
                _source->query(_prefix, ids, 16, off);
                const uint16_t got = static_cast<uint16_t>(std::min<uint32_t>(16, total - off));
                for (uint16_t i = 0; i < got; i++) {
                    if (ids[i] == static_cast<uint16_t>(id)) {
                        _page = static_cast<uint16_t>((off + i) / kCandCells);
                        break;
                    }
                }
            }
        }
    }
    refresh();
}

// ---------------------------------------------------------------------------
// input state

bool DialView::typeLetter(char c)
{
    if (_root == nullptr || c < 'a' || c > 'z' || !_valid[c - 'a']) {
        return false;
    }
    _prefix[_len++] = c;
    _prefix[_len]   = '\0';
    _page           = 0;
    refresh();
    return true;
}

void DialView::deleteLetter()
{
    if (_root == nullptr || _len == 0) {
        return;
    }
    const char removed = _prefix[_len - 1];
    _len--;
    _prefix[_len] = '\0';
    _page         = 0;
    refresh();
    flyBack(removed);
}

bool DialView::eligible(uint8_t slot) const
{
    if (_valid[slot]) {
        return true;
    }
    // The last-typed letter takes hits even though it cannot continue the
    // prefix (pinyin has no doubled letters): tapping it takes it back.
    return _len > 0 && _prefix[_len - 1] == static_cast<char>('a' + slot);
}

int8_t DialView::slotForPoint(int32_t x, int32_t y) const
{
    const float dx = static_cast<float>(x - kScreenC);
    const float dy = static_cast<float>(y - kScreenC);
    const float r  = std::sqrt(dx * dx + dy * dy);
    if (r < kRingHitMin || r > kRingHitMax) {
        return -1;
    }
    // Continuous slot position with 'a' at 12 o'clock, clockwise.
    float s = (std::atan2(dy, dx) * 180.0f / static_cast<float>(M_PI) + 90.0f) / kSlotDeg;
    while (s < 0) {
        s += kLetters;
    }
    int8_t best      = -1;
    float best_dist  = kSnapSlots + 1.0f;
    const int centre = static_cast<int>(std::lround(s));
    for (int o = -3; o <= 3; o++) {
        const int slot = ((centre + o) % kLetters + kLetters) % kLetters;
        float dist     = std::fabs(static_cast<float>(centre + o) - s);
        if (dist > kSnapSlots || !eligible(static_cast<uint8_t>(slot))) {
            continue;
        }
        if (dist < best_dist) {
            best_dist = dist;
            best      = static_cast<int8_t>(slot);
        }
    }
    return best;
}

void DialView::armSlot(int8_t slot)
{
    if (_armed == slot) {
        return;
    }
    if (_armed >= 0) {
        lv_obj_t* old = _letters[_armed];
        lv_obj_set_style_transform_scale(old, 256, 0);
        lv_obj_set_style_translate_x(old, 0, 0);
        lv_obj_set_style_translate_y(old, 0, 0);
    }
    _armed = slot;
    if (slot >= 0) {
        lv_obj_t* label = _letters[slot];
        const float a   = slotAngleRad(static_cast<uint8_t>(slot));
        lv_obj_set_style_transform_scale(label, kMagnifyScale, 0);
        lv_obj_set_style_translate_x(label, static_cast<int16_t>(-kMagnifyLift * std::cos(a)), 0);
        lv_obj_set_style_translate_y(label, static_cast<int16_t>(-kMagnifyLift * std::sin(a)), 0);
    }
    layoutEcho();
}

void DialView::handleRingPress(int32_t x, int32_t y)
{
    const int8_t slot = slotForPoint(x, y);
    if (slot >= 0) {
        GetHAL().vibrate(5);
        armSlot(slot);
    }
}

void DialView::handleRingMove(int32_t x, int32_t y)
{
    if (_armed < 0) {
        return;
    }
    const int8_t slot = slotForPoint(x, y);
    // Sliding off every eligible letter keeps the last one armed: forgiving
    // for small fingers, and release still commits what the magnifier shows.
    if (slot >= 0 && slot != _armed) {
        GetHAL().vibrate(5);
        armSlot(slot);
    }
}

void DialView::handleRingRelease()
{
    if (_armed < 0) {
        return;
    }
    const uint8_t slot = static_cast<uint8_t>(_armed);
    armSlot(-1);
    GetHAL().vibrate(15);
    if (_len > 0 && _prefix[_len - 1] == static_cast<char>('a' + slot)) {
        deleteLetter();
    } else {
        typeLetter(static_cast<char>('a' + slot));
    }
}

void DialView::handleRingAbort()
{
    armSlot(-1);
}

void DialView::handleEchoShort()
{
    if (_len == 0) {
        return;
    }
    GetHAL().vibrate(15);
    deleteLetter();
}

void DialView::handleEchoLong()
{
    if (_len == 0) {
        return;
    }
    GetHAL().vibrate(30);
    reset();
}

void DialView::handleCandidate(uint8_t cell)
{
    if (_root == nullptr || cell >= kCandCells || !_cands[cell].occupied) {
        return;
    }
    GetHAL().vibrate(20);
    _pick_id = _cands[cell].id;
    matchedReading(_pick_id, _pick_reading, sizeof(_pick_reading));
    _pick_pending = true;
}

void DialView::nextCandidatePage()
{
    const uint16_t pages = static_cast<uint16_t>((_cand_total + kCandCells - 1) / kCandCells);
    if (_page + 1 < pages) {
        _page++;
        refreshCandidates();
    }
}

void DialView::previousCandidatePage()
{
    if (_page > 0) {
        _page--;
        refreshCandidates();
    }
}

// ---------------------------------------------------------------------------
// rendering

void DialView::matchedReading(uint16_t id, char* out, size_t cap) const
{
    out[0]              = '\0';
    const char* caption = _painter->caption(id);
    if (caption == nullptr || caption[0] == '\0') {
        return;
    }
    const size_t prefix_len    = _len;
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
            if (pyNormalize(token, plain, sizeof(plain)) > 0 && std::strncmp(plain, _prefix, prefix_len) == 0) {
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

void DialView::refreshRing()
{
    char probe[kMaxLen + 2];
    std::memcpy(probe, _prefix, _len);
    for (uint8_t i = 0; i < kLetters; i++) {
        bool valid = false;
        if (_len < kMaxLen) {
            probe[_len]     = static_cast<char>('a' + i);
            probe[_len + 1] = '\0';
            valid           = _source->query(probe, nullptr, 0, 0) > 0;
        }
        _valid[i] = valid;
    }

    const int8_t recall = _len > 0 ? static_cast<int8_t>(_prefix[_len - 1] - 'a') : static_cast<int8_t>(-1);
    for (uint8_t i = 0; i < kLetters; i++) {
        const bool bright = _valid[i] || i == recall;
        lv_obj_set_style_text_color(_letters[i], lv_color_hex(bright ? kInk : kDimmed), 0);
    }
    if (recall >= 0) {
        const float a = slotAngleRad(static_cast<uint8_t>(recall));
        lv_obj_align(_recall_dot, LV_ALIGN_CENTER, static_cast<int16_t>(kDotR * std::cos(a)),
                     static_cast<int16_t>(kDotR * std::sin(a)));
        lv_obj_clear_flag(_recall_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_recall_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

void DialView::layoutEcho()
{
    // While a ring press is in flight the armed letter shows greyed at the
    // echo -- committing turns it white; arming the recall letter greys the
    // last committed letter instead (about to be taken back).
    const int8_t recall     = _len > 0 ? static_cast<int8_t>(_prefix[_len - 1] - 'a') : static_cast<int8_t>(-1);
    const bool recall_armed = _armed >= 0 && _armed == recall;
    char committed[kMaxLen + 1];
    std::memcpy(committed, _prefix, _len + 1);
    char pending[2] = {'\0', '\0'};
    if (recall_armed) {
        committed[_len - 1] = '\0';
        pending[0]          = _prefix[_len - 1];
    } else if (_armed >= 0) {
        pending[0] = static_cast<char>('a' + _armed);
    }

    if (committed[0] == '\0' && pending[0] == '\0') {
        lv_obj_add_flag(_echo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_pending, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_empty_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(_empty_hint, LV_OBJ_FLAG_HIDDEN);

    lv_point_t sz;
    int32_t w_committed = 0;
    if (committed[0] != '\0') {
        lv_text_get_size(&sz, committed, &lv_font_hanzi_pinyin_44, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        w_committed = sz.x;
    }
    int32_t w_pending = 0;
    if (pending[0] != '\0') {
        lv_text_get_size(&sz, pending, &lv_font_hanzi_pinyin_44, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        w_pending = sz.x;
    }
    const int32_t gap   = (w_committed > 0 && w_pending > 0) ? 4 : 0;
    const int32_t total = w_committed + gap + w_pending;

    if (committed[0] != '\0') {
        lv_label_set_text(_echo, committed);
        lv_obj_align(_echo, LV_ALIGN_CENTER, static_cast<int16_t>(-total / 2 + w_committed / 2), kEchoY);
        lv_obj_clear_flag(_echo, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_echo, LV_OBJ_FLAG_HIDDEN);
    }
    if (pending[0] != '\0') {
        lv_label_set_text(_pending, pending);
        lv_obj_align(_pending, LV_ALIGN_CENTER, static_cast<int16_t>(total / 2 - w_pending / 2), kEchoY);
        lv_obj_clear_flag(_pending, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_pending, LV_OBJ_FLAG_HIDDEN);
    }
}

void DialView::refreshCandidates()
{
    const size_t glyph_px = static_cast<size_t>(kCandGlyph) * kCandGlyph;
    uint16_t ids[kCandCells];
    uint16_t got = 0;
    if (_len > 0) {
        _cand_total = _source->query(_prefix, ids, kCandCells, static_cast<uint16_t>(_page * kCandCells));
        if (_page > 0 && static_cast<uint32_t>(_page) * kCandCells >= _cand_total) {
            _page       = 0;
            _cand_total = _source->query(_prefix, ids, kCandCells, 0);
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
        lv_obj_clear_flag(_page_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_page_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void DialView::refresh()
{
    if (_root == nullptr) {
        return;
    }
    refreshRing();
    layoutEcho();
    refreshCandidates();
}

void DialView::flyBack(char letter)
{
    // The deleted letter drifts from the echo back to its seat on the ring:
    // a purely decorative one-shot; the label deletes itself when the
    // animation completes (or with the page root, whichever comes first).
    const uint8_t slot = static_cast<uint8_t>(letter - 'a');
    lv_obj_t* ghost    = lv_label_create(_root);
    lv_obj_set_style_text_font(ghost, &lv_font_pinyin_latin_32, 0);
    lv_obj_set_style_text_color(ghost, lv_color_hex(kGrey), 0);
    char text[2] = {letter, '\0'};
    lv_label_set_text(ghost, text);
    lv_obj_clear_flag(ghost, LV_OBJ_FLAG_CLICKABLE);

    const float a    = slotAngleRad(slot);
    const int32_t x0 = kScreenC + 20;  // just right of the echo text
    const int32_t y0 = kScreenC + kEchoY;
    const int32_t x1 = kScreenC + static_cast<int32_t>(kRingR * std::cos(a));
    const int32_t y1 = kScreenC + static_cast<int32_t>(kRingR * std::sin(a));

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, ghost);
    lv_anim_set_exec_cb(&ax, [](void* obj, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t*>(obj), v); });
    lv_anim_set_values(&ax, x0, x1);
    lv_anim_set_duration(&ax, 220);
    lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&ax, flyBackDeleteCb);
    lv_anim_start(&ax);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, ghost);
    lv_anim_set_exec_cb(&ay, [](void* obj, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), v); });
    lv_anim_set_values(&ay, y0, y1);
    lv_anim_set_duration(&ay, 220);
    lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
    lv_anim_start(&ay);
}

namespace {

void flyBackDeleteCb(lv_anim_t* anim)
{
    lv_obj_delete(static_cast<lv_obj_t*>(anim->var));
}

DialView* viewOf(lv_event_t* e)
{
    return static_cast<DialView*>(lv_event_get_user_data(e));
}

void ringEventCb(lv_event_t* e)
{
    DialView* view = viewOf(e);
    if (view == nullptr) {
        return;
    }
    lv_indev_t* dev            = lv_indev_active();
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        view->handleRingRelease();
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        view->handleRingAbort();
        return;
    }
    if (dev == nullptr) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(dev, &p);
    if (code == LV_EVENT_PRESSED) {
        view->handleRingPress(p.x, p.y);
    } else if (code == LV_EVENT_PRESSING) {
        view->handleRingMove(p.x, p.y);
    }
}

void echoShortCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleEchoShort();
    }
}

void echoLongCb(lv_event_t* e)
{
    if (auto* view = viewOf(e)) {
        view->handleEchoLong();
    }
}

void candidateCb(lv_event_t* e)
{
    auto* view = viewOf(e);
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (view == nullptr || obj == nullptr) {
        return;
    }
    view->handleCandidate(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj))));
}

}  // namespace

}  // namespace pime
