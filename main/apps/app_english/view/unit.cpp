/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <assets/assets.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "view.h"

using namespace eng_view;

namespace {

constexpr const char* kTag = "EngUnitPage";

// Units sit on a ring, read clockwise from the top -- the same idea as the
// arithmetic map, so a child who has used that one already knows this.
//
// The button hints own the top of the glass -- they belong under the two bezel
// buttons at (+/-95, -145), and that row runs from y=-160 down to y=-130. The
// ring gets out of its way by dropping 30 px rather than by shrinking: at
// r=122 neighbouring tiles are 93 px apart and leave 17 px of black between
// them, and pulling the radius in to clear the hints closed that to 7 px, so
// the tiles read as one crowded block. Moving the whole ring down instead
// costs nothing -- the space the hints vacated at the bottom is exactly what
// it moves into -- and puts the top tile's edge at y=-130, level with the
// bottom of the hint row.
//
// The centre text rides down with the ring, or it stops looking like the
// ring's centre.
constexpr int16_t kRingRadius = 122;
constexpr int16_t kRingCy     = 30;
constexpr int16_t kTileW      = 76;
constexpr int16_t kTileH      = 76;
constexpr int16_t kCounterY   = -46 + kRingCy;
constexpr int16_t kTitleY     = -18 + kRingCy;
constexpr int16_t kDotsY      = 24 + kRingCy;
constexpr int16_t kHintY      = -145;
constexpr int16_t kHintX      = 95;
constexpr int16_t kDotSize    = 9;
constexpr int16_t kDotGap     = 15;

struct Point {
    int16_t x;
    int16_t y;
};

/// Position of tile `index` of `count`, evenly spaced around the ring,
/// starting at twelve o'clock and running clockwise.
///
/// An earlier version snapped onto a 12-position clock face to avoid libm.
/// That is fine for 6 units (every other hour) but visibly lopsided for 5:
/// index*12/5 lands on 0, 2, 4, 7, 9 -- gaps of 60/60/90/60/90 degrees, which
/// reads as a mistake rather than a layout. The ESP32-S3 has a hardware FPU
/// and this runs a handful of times per redraw, so the honest trig is free.
Point ringPoint(uint8_t index, uint8_t count)
{
    if (count == 0) {
        return {0, kRingCy};
    }
    const float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(index) / static_cast<float>(count);
    return {static_cast<int16_t>(kRingRadius * std::sin(angle)),
            static_cast<int16_t>(kRingCy - kRingRadius * std::cos(angle))};
}

void tileClickedCb(lv_event_t* e)
{
    auto* page = static_cast<UnitPage*>(lv_event_get_user_data(e));
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (page == nullptr || obj == nullptr) {
        return;
    }
    const auto raw = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)));
    // Stored as unit+1 so that the centre can use 0 and still be distinct from
    // "unit zero". show() rewrites it every time the page turns.
    page->handleClicked(static_cast<int16_t>(static_cast<int>(raw) - 1));
}

}  // namespace

UnitPage::~UnitPage()
{
    destroy();
}

bool UnitPage::create(lv_obj_t* parent)
{
    if (parent == nullptr) {
        mclog::tagError(kTag, "no parent");
        return false;
    }

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    // Centre tap target: big, invisible, "just start the one that is already
    // chosen". The arithmetic map page has had one since it was written and
    // takeTap() has always documented -1 for it, but nothing here ever sent a
    // -1: the middle of this page was dead glass. Created before the tiles so
    // they stay on top and keep the taps near the ring.
    _centre = lv_obj_create(_root);
    lv_obj_remove_style_all(_centre);
    lv_obj_set_size(_centre, 160, 160);
    lv_obj_align(_centre, LV_ALIGN_CENTER, 0, kRingCy);
    lv_obj_clear_flag(_centre, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_centre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(_centre, reinterpret_cast<void*>(static_cast<uintptr_t>(0)));
    lv_obj_add_event_cb(_centre, tileClickedCb, LV_EVENT_CLICKED, this);

    for (uint8_t i = 0; i < kTilesPerPage; ++i) {
        _tiles[i] = lv_obj_create(_root);
        lv_obj_remove_style_all(_tiles[i]);
        lv_obj_set_size(_tiles[i], kTileW, kTileH);
        lv_obj_clear_flag(_tiles[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_tiles[i], 16, 0);
        lv_obj_set_style_bg_opa(_tiles[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(_tiles[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_tiles[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(_tiles[i], tileClickedCb, LV_EVENT_CLICKED, this);

        _labels[i] = lv_label_create(_tiles[i]);
        lv_obj_set_style_text_font(_labels[i], &lv_font_hanzi_ui_24, 0);
        lv_obj_set_style_text_align(_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        // Unit names run to four characters ("学习用品", "零食饮料"), which is
        // 96 px against a 68 px text area. Wrapping puts them on two lines
        // instead of letting them spill over the tile edge.
        lv_obj_set_width(_labels[i], kTileW - 8);
        lv_label_set_long_mode(_labels[i], LV_LABEL_LONG_WRAP);
        lv_obj_center(_labels[i]);
        lv_label_set_text(_labels[i], "");
    }

    // Centre: the selected unit's name, its best rating, and an invisible tap
    // target that means "start".
    _title = lv_label_create(_root);
    lv_obj_set_style_text_font(_title, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_title, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_title, "英语");
    lv_obj_align(_title, LV_ALIGN_CENTER, 0, kTitleY);

    for (uint8_t d = 0; d < 3; ++d) {
        _dots[d] = lv_obj_create(_root);
        lv_obj_remove_style_all(_dots[d]);
        lv_obj_set_size(_dots[d], kDotSize, kDotSize);
        lv_obj_set_style_radius(_dots[d], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(_dots[d], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(_dots[d], lv_color_hex(kCardBorder), 0);
        lv_obj_align(_dots[d], LV_ALIGN_CENTER, static_cast<int16_t>((d - 1) * kDotGap), kDotsY);
    }

    _counter = lv_label_create(_root);
    lv_obj_set_style_text_font(_counter, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_counter, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_counter, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_counter, "");
    lv_obj_align(_counter, LV_ALIGN_CENTER, 0, kCounterY);

    _hint_a = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_a, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_a, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_a, "A 换一关");
    lv_obj_align(_hint_a, LV_ALIGN_CENTER, -kHintX, kHintY);

    _hint_b = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_b, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_b, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_b, "B 开始");
    lv_obj_align(_hint_b, LV_ALIGN_CENTER, kHintX, kHintY);

    return true;
}

void UnitPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    for (uint8_t i = 0; i < kTilesPerPage; ++i) {
        _tiles[i]  = nullptr;
        _labels[i] = nullptr;
    }
    for (uint8_t d = 0; d < 3; ++d) {
        _dots[d] = nullptr;
    }
    _title   = nullptr;
    _counter = nullptr;
    _centre  = nullptr;
    _hint_a  = nullptr;
    _hint_b  = nullptr;
    _shown   = 0;
}

void UnitPage::setHidden(bool hidden)
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

void UnitPage::show(const eng::Data& data, uint16_t selected, const uint8_t* best_stars)
{
    if (_root == nullptr) {
        return;
    }

    const uint16_t total = data.unitCount();
    if (total == 0) {
        return;
    }
    // Draw whichever page the selection sits on, so stepping past the last
    // tile turns the page instead of running off the end of the ring.
    const uint16_t page_first = static_cast<uint16_t>((selected / kTilesPerPage) * kTilesPerPage);
    const uint8_t count       = static_cast<uint8_t>(std::min<uint16_t>(kTilesPerPage, total - page_first));
    _shown                    = count;

    for (uint8_t i = 0; i < kTilesPerPage; ++i) {
        if (i >= count) {
            lv_obj_add_flag(_tiles[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const uint16_t unit_index = static_cast<uint16_t>(page_first + i);
        const eng::Unit u         = data.unit(unit_index);
        const Point p             = ringPoint(i, count);
        lv_obj_clear_flag(_tiles[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(_tiles[i], LV_ALIGN_CENTER, p.x, p.y);
        lv_label_set_text(_labels[i], u.title != nullptr ? u.title : "");
        // The tap target carries the *global* unit index: a tile only means
        // whatever the current page put on it.
        lv_obj_set_user_data(_tiles[i], reinterpret_cast<void*>(static_cast<uintptr_t>(unit_index + 1)));

        const bool is_selected = (unit_index == selected);
        lv_obj_set_style_bg_color(_tiles[i], lv_color_hex(is_selected ? kRightBg : kCardBg), 0);
        lv_obj_set_style_border_width(_tiles[i], is_selected ? 3 : 2, 0);
        lv_obj_set_style_border_color(_tiles[i], lv_color_hex(is_selected ? kAccent : kCardBorder), 0);
        lv_obj_set_style_text_color(_labels[i], lv_color_hex(is_selected ? kInk : kDim), 0);
    }

    const eng::Unit sel = data.unit(selected);
    lv_label_set_text(_title, sel.title != nullptr ? sel.title : "英语");
    lv_obj_align(_title, LV_ALIGN_CENTER, 0, kTitleY);

    char counter[16];
    std::snprintf(counter, sizeof(counter), "%u/%u", selected + 1u, total);
    lv_label_set_text(_counter, counter);
    lv_obj_align(_counter, LV_ALIGN_CENTER, 0, kCounterY);

    const uint8_t stars = best_stars != nullptr ? best_stars[selected] : 0;
    for (uint8_t d = 0; d < 3; ++d) {
        if (_dots[d] == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(_dots[d], lv_color_hex(d < stars ? kAccent : kCardBorder), 0);
    }
}

void UnitPage::handleClicked(int16_t unit)
{
    _tap_pending = true;
    _tap_unit    = unit;
}

bool UnitPage::takeTap(int16_t& unit)
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    unit         = _tap_unit;
    return true;
}

void UnitPage::clearTap()
{
    _tap_pending = false;
}
