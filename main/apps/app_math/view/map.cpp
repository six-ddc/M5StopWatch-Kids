/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdint>
#include <cstdio>
#include "view.h"

using namespace view;

namespace {

constexpr const char* kTag = "MapPage";

// Six nodes on a ring of radius 158, clockwise from the top -- the round
// screen's own shape is the path. A 64 px node at r=158 reaches out to
// r=158+45 (corner) which stays inside the 233 px glass.
constexpr int16_t kNodeSize = 64;
struct NodePos {
    int16_t x;
    int16_t y;
};
constexpr NodePos kNodePos[math::kLevelCount] = {
    {0, -158}, {137, -79}, {137, 79}, {0, 158}, {-137, 79}, {-137, -79},
};

constexpr int16_t kHintY    = -145;
constexpr int16_t kHintX    = 95;
constexpr int16_t kNameY    = -36;
constexpr int16_t kTierY    = -2;
constexpr int16_t kBestY    = 40;
constexpr int16_t kWalletY  = 80;

constexpr uint32_t kInk        = 0xF2F0EA;
constexpr uint32_t kDim        = 0x8A8A88;
constexpr uint32_t kFaint      = 0x4A4A48;
constexpr uint32_t kStarOn     = 0xE8B84B;
constexpr uint32_t kStarOff    = 0x2E2E2E;
constexpr uint32_t kDust       = 0x4BD0C0;
constexpr uint32_t kCardBg     = 0x141414;
constexpr uint32_t kCardBorder = 0x3C3C3C;
constexpr uint32_t kLockedBg   = 0x0A0A0A;
constexpr uint32_t kLockedEdge = 0x1E1E1E;

void nodeClickedCb(lv_event_t* e)
{
    auto* page = static_cast<MapPage*>(lv_event_get_user_data(e));
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (page == nullptr || obj == nullptr) {
        return;
    }
    // Node index is stashed in the object's user data; the centre target
    // carries -1 and means "start the selected one".
    const auto tag = static_cast<intptr_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
    page->handleClicked(static_cast<int8_t>(tag));
}

lv_obj_t* makeDot(lv_obj_t* parent, int16_t size, uint32_t color)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, size, size);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    return dot;
}

}  // namespace

MapPage::~MapPage()
{
    destroy();
}

bool MapPage::create(lv_obj_t* parent)
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

    // Centre tap target: big and invisible, "just start". Created before the
    // nodes so they stay on top and win the taps near the rim.
    _centre = lv_obj_create(_root);
    lv_obj_remove_style_all(_centre);
    lv_obj_set_size(_centre, 200, 200);
    lv_obj_center(_centre);
    lv_obj_clear_flag(_centre, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_centre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(_centre, reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    lv_obj_add_event_cb(_centre, nodeClickedCb, LV_EVENT_CLICKED, this);

    for (uint8_t i = 0; i < math::kLevelCount; ++i) {
        _nodes[i] = lv_obj_create(_root);
        lv_obj_remove_style_all(_nodes[i]);
        lv_obj_set_size(_nodes[i], kNodeSize, kNodeSize);
        lv_obj_align(_nodes[i], LV_ALIGN_CENTER, kNodePos[i].x, kNodePos[i].y);
        lv_obj_clear_flag(_nodes[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_nodes[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(_nodes[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(_nodes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(_nodes[i], reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(_nodes[i], nodeClickedCb, LV_EVENT_CLICKED, this);

        _node_labels[i] = lv_label_create(_nodes[i]);
        lv_obj_set_style_text_font(_node_labels[i], &lv_font_hanzi_ui_24, 0);
        char num[4];
        std::snprintf(num, sizeof(num), "%u", i + 1);
        lv_label_set_text(_node_labels[i], num);
        lv_obj_align(_node_labels[i], LV_ALIGN_CENTER, 0, -8);

        for (uint8_t s = 0; s < 3; ++s) {
            _node_dots[i][s] = makeDot(_nodes[i], 7, kStarOff);
            lv_obj_align(_node_dots[i][s], LV_ALIGN_CENTER,
                         static_cast<int16_t>((s - 1) * 11), 15);
        }
    }

    _name = lv_label_create(_root);
    lv_obj_set_style_text_font(_name, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_name, lv_color_hex(kInk), 0);
    lv_label_set_text(_name, "");
    lv_obj_align(_name, LV_ALIGN_CENTER, 0, kNameY);

    _tier_name = lv_label_create(_root);
    lv_obj_set_style_text_font(_tier_name, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_tier_name, lv_color_hex(kDim), 0);
    lv_label_set_text(_tier_name, "");
    lv_obj_align(_tier_name, LV_ALIGN_CENTER, 0, kTierY);

    for (uint8_t s = 0; s < 3; ++s) {
        _best_dots[s] = makeDot(_root, 14, kStarOff);
        lv_obj_align(_best_dots[s], LV_ALIGN_CENTER, static_cast<int16_t>((s - 1) * 24), kBestY);
    }

    _wallet_row = lv_obj_create(_root);
    lv_obj_remove_style_all(_wallet_row);
    lv_obj_set_size(_wallet_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_wallet_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_wallet_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_wallet_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_wallet_row, 9, 0);

    makeDot(_wallet_row, 20, kStarOn);
    _stars_label = lv_label_create(_wallet_row);
    lv_obj_set_style_text_font(_stars_label, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_stars_label, lv_color_hex(kInk), 0);
    lv_label_set_text(_stars_label, "0");

    lv_obj_t* dust_icon = makeDot(_wallet_row, 12, kDust);
    lv_obj_set_style_margin_left(dust_icon, 12, 0);
    _dust_label = lv_label_create(_wallet_row);
    lv_obj_set_style_text_font(_dust_label, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_dust_label, lv_color_hex(kInk), 0);
    lv_label_set_text(_dust_label, "0");

    lv_obj_align(_wallet_row, LV_ALIGN_CENTER, 0, kWalletY);

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

void MapPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    for (uint8_t i = 0; i < math::kLevelCount; ++i) {
        _nodes[i]       = nullptr;
        _node_labels[i] = nullptr;
        for (uint8_t s = 0; s < 3; ++s) {
            _node_dots[i][s] = nullptr;
        }
    }
    for (uint8_t s = 0; s < 3; ++s) {
        _best_dots[s] = nullptr;
    }
    _centre      = nullptr;
    _name        = nullptr;
    _tier_name   = nullptr;
    _wallet_row  = nullptr;
    _stars_label = nullptr;
    _dust_label  = nullptr;
    _hint_a      = nullptr;
    _hint_b      = nullptr;
}

void MapPage::setHidden(bool hidden)
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

void MapPage::show(const MapInfo& info)
{
    if (_root == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < math::kLevelCount; ++i) {
        const bool unlocked = i <= info.max_unlocked;
        const bool selected = i == info.selected;

        lv_obj_set_style_bg_color(_nodes[i], lv_color_hex(unlocked ? kCardBg : kLockedBg), 0);
        lv_obj_set_style_border_width(_nodes[i], selected ? 4 : 2, 0);
        lv_obj_set_style_border_color(
            _nodes[i],
            lv_color_hex(selected ? kStarOn : (unlocked ? kCardBorder : kLockedEdge)), 0);
        lv_obj_set_style_text_color(_node_labels[i], lv_color_hex(unlocked ? kInk : kFaint), 0);

        for (uint8_t s = 0; s < 3; ++s) {
            if (unlocked) {
                lv_obj_clear_flag(_node_dots[i][s], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(_node_dots[i][s],
                                          lv_color_hex(s < info.best_stars[i] ? kStarOn
                                                                              : kStarOff),
                                          0);
            } else {
                lv_obj_add_flag(_node_dots[i][s], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    const auto level = static_cast<math::Level>(info.selected);
    lv_label_set_text(_name, math::levelKidName(level));
    lv_obj_align(_name, LV_ALIGN_CENTER, 0, kNameY);
    lv_label_set_text(_tier_name, math::levelName(level));
    lv_obj_align(_tier_name, LV_ALIGN_CENTER, 0, kTierY);

    for (uint8_t s = 0; s < 3; ++s) {
        lv_obj_set_style_bg_color(
            _best_dots[s],
            lv_color_hex(s < info.best_stars[info.selected] ? kStarOn : kStarOff), 0);
    }

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", info.wallet.stars);
    lv_label_set_text(_stars_label, buf);
    std::snprintf(buf, sizeof(buf), "%u", info.wallet.dust);
    lv_label_set_text(_dust_label, buf);
    lv_obj_align(_wallet_row, LV_ALIGN_CENTER, 0, kWalletY);
}

bool MapPage::takeTap(int8_t& node)
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    node         = _tap_node;
    return true;
}

void MapPage::clearTap()
{
    _tap_pending = false;
}

void MapPage::handleClicked(int8_t node)
{
    // Runs under the LVGL lock; only records the tap. The app drains it from
    // onRunning, where locking and NVS are legal.
    _tap_pending = true;
    _tap_node    = node;
}
