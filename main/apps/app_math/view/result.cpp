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

constexpr const char* kTag = "ResultPage";

// The buttons sit like ears at roughly 10 and 2 o'clock, so each hint goes
// under the button it belongs to instead of sharing one line at the bottom.
constexpr int16_t kHintY      = -145;
constexpr int16_t kHintX      = 95;
constexpr int16_t kLevelY     = -103;
constexpr int16_t kDotY       = -55;
constexpr int16_t kDotSize    = 28;
constexpr int16_t kDotGap     = 44;
constexpr int16_t kScoreY     = 25;
constexpr int16_t kVerdictY   = 110;
constexpr int16_t kWalletY    = 150;

constexpr uint32_t kInk      = 0xF2F0EA;
constexpr uint32_t kDim      = 0x8A8A88;
constexpr uint32_t kStarOn   = 0xE8B84B;
constexpr uint32_t kStarOff  = 0x2E2E2E;
constexpr uint32_t kDust     = 0x4BD0C0;  // the streak ring's warm teal
constexpr uint32_t kCardBg     = 0x141414;
constexpr uint32_t kCardBorder = 0x3C3C3C;

void rootClickedCb(lv_event_t* e)
{
    auto* page = static_cast<ResultPage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->handleClicked();
    }
}

// One-shot scale pulse for a celebration moment: up a third, back down.
// Anchored to the object, so LVGL cleans it up if the page dies mid-pulse.
void scalePulseCb(void* var, int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(var), value, 0);
}

void startScalePulse(lv_obj_t* obj)
{
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, scalePulseCb);
    lv_anim_set_values(&a, 256, 340);
    lv_anim_set_duration(&a, 110);
    lv_anim_set_playback_duration(&a, 140);
    lv_anim_start(&a);
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

ResultPage::~ResultPage()
{
    destroy();
}

bool ResultPage::create(lv_obj_t* parent)
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
    // The whole screen is the "next round" button: a child should not have to
    // aim at anything to keep playing.
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, rootClickedCb, LV_EVENT_CLICKED, this);

    // Three filled circles rather than star glyphs -- the subset font has no
    // star, and a circle needs no font at all.
    for (uint8_t i = 0; i < 3; ++i) {
        _dots[i] = makeDot(_root, kDotSize, kStarOff);
        lv_obj_align(_dots[i], LV_ALIGN_CENTER, static_cast<int16_t>((i - 1) * kDotGap), kDotY);
    }

    _score = lv_label_create(_root);
    lv_obj_set_style_text_font(_score, &lv_font_digit_96, 0);
    lv_obj_set_style_text_color(_score, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_score, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_score, "");
    lv_obj_align(_score, LV_ALIGN_CENTER, 0, kScoreY);

    _verdict = lv_label_create(_root);
    lv_obj_set_style_text_font(_verdict, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_verdict, lv_color_hex(kStarOn), 0);
    lv_obj_set_style_text_align(_verdict, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_verdict, "");
    lv_obj_align(_verdict, LV_ALIGN_CENTER, 0, kVerdictY);

    _level = lv_label_create(_root);
    lv_obj_set_style_text_font(_level, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_level, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_level, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_level, "");
    lv_obj_align(_level, LV_ALIGN_CENTER, 0, kLevelY);

    _hint_a = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_a, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_a, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_a, "A 再来一关");
    lv_obj_align(_hint_a, LV_ALIGN_CENTER, -kHintX, kHintY);

    _hint_b = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_b, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_b, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_b, "B 地图");
    lv_obj_align(_hint_b, LV_ALIGN_CENTER, kHintX, kHintY);

    // Wallet: star icon + count, dust icon + count. A flex row sized to its
    // content, so it stays centred whatever the digit counts are.
    _wallet_row = lv_obj_create(_root);
    lv_obj_remove_style_all(_wallet_row);
    lv_obj_set_size(_wallet_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(_wallet_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_wallet_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_wallet_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_wallet_row, 9, 0);

    _wallet_star = makeDot(_wallet_row, 20, kStarOn);
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

    // Unlock overlay: hidden until a star threshold is crossed. It advertises
    // the new mode by showing it -- the two 对 / 错 cards -- rather than
    // describing it.
    _unlock = lv_obj_create(_root);
    lv_obj_remove_style_all(_unlock);
    lv_obj_set_size(_unlock, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_unlock);
    lv_obj_clear_flag(_unlock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_unlock, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_unlock, LV_OPA_COVER, 0);
    lv_obj_add_flag(_unlock, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* unlock_title = lv_label_create(_unlock);
    lv_obj_set_style_text_font(unlock_title, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(unlock_title, lv_color_hex(kDim), 0);
    lv_label_set_text(unlock_title, "解锁新玩法");
    lv_obj_align(unlock_title, LV_ALIGN_CENTER, 0, -130);

    for (uint8_t i = 0; i < 2; ++i) {
        _unlock_cards[i] = lv_obj_create(_unlock);
        lv_obj_remove_style_all(_unlock_cards[i]);
        lv_obj_set_size(_unlock_cards[i], 110, 80);
        lv_obj_align(_unlock_cards[i], LV_ALIGN_CENTER, i == 0 ? -70 : 70, -10);
        lv_obj_clear_flag(_unlock_cards[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_unlock_cards[i], 16, 0);
        lv_obj_set_style_bg_opa(_unlock_cards[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(_unlock_cards[i], lv_color_hex(kCardBg), 0);
        lv_obj_set_style_border_width(_unlock_cards[i], 3, 0);
        lv_obj_set_style_border_color(_unlock_cards[i], lv_color_hex(kCardBorder), 0);

        lv_obj_t* text = lv_label_create(_unlock_cards[i]);
        lv_obj_set_style_text_font(text, &lv_font_digit_64, 0);
        lv_obj_set_style_text_color(text, lv_color_hex(kInk), 0);
        lv_label_set_text(text, i == 0 ? "对" : "错");
        lv_obj_center(text);
    }

    _unlock_name = lv_label_create(_unlock);
    lv_obj_set_style_text_font(_unlock_name, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_unlock_name, lv_color_hex(kStarOn), 0);
    lv_label_set_text(_unlock_name, "");
    lv_obj_align(_unlock_name, LV_ALIGN_CENTER, 0, 80);

    lv_obj_t* unlock_hint = lv_label_create(_unlock);
    lv_obj_set_style_text_font(unlock_hint, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(unlock_hint, lv_color_hex(kDim), 0);
    lv_label_set_text(unlock_hint, "点一下继续");
    lv_obj_align(unlock_hint, LV_ALIGN_CENTER, 0, 135);

    return true;
}

void ResultPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        _dots[i] = nullptr;
    }
    _score       = nullptr;
    _verdict     = nullptr;
    _level       = nullptr;
    _hint_a      = nullptr;
    _hint_b      = nullptr;
    _wallet_row  = nullptr;
    _wallet_star = nullptr;
    _stars_label = nullptr;
    _dust_label  = nullptr;
    _unlock      = nullptr;
    _unlock_name = nullptr;
    _unlock_cards[0] = nullptr;
    _unlock_cards[1] = nullptr;
}

void ResultPage::setHidden(bool hidden)
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

void ResultPage::beginSummary(const Summary& summary)
{
    if (_root == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_set_style_bg_color(_dots[i], lv_color_hex(kStarOff), 0);
        lv_obj_set_style_transform_scale(_dots[i], 256, 0);
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u / %u", summary.correct, summary.total);
    lv_label_set_text(_score, buf);
    lv_obj_align(_score, LV_ALIGN_CENTER, 0, kScoreY);

    lv_label_set_text(_verdict, summary.verdict != nullptr ? summary.verdict : "");
    lv_obj_align(_verdict, LV_ALIGN_CENTER, 0, kVerdictY);

    showLevel(summary.level);
    showWallet(summary.wallet_before.stars, summary.wallet_before.dust);
    hideUnlock();
}

void ResultPage::revealStar(uint8_t index)
{
    if (_root == nullptr || index >= 3) {
        return;
    }
    lv_obj_set_style_bg_color(_dots[index], lv_color_hex(kStarOn), 0);
    startScalePulse(_dots[index]);
}

void ResultPage::showWallet(uint16_t stars, uint8_t dust)
{
    if (_root == nullptr) {
        return;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", stars);
    lv_label_set_text(_stars_label, buf);
    std::snprintf(buf, sizeof(buf), "%u", dust);
    lv_label_set_text(_dust_label, buf);
    lv_obj_align(_wallet_row, LV_ALIGN_CENTER, 0, kWalletY);
}

void ResultPage::pulseWalletStar()
{
    if (_wallet_star != nullptr) {
        startScalePulse(_wallet_star);
    }
}

void ResultPage::finishSummary(const Summary& summary, const math::Wallet& wallet_after)
{
    if (_root == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_set_style_bg_color(_dots[i], lv_color_hex(i < summary.stars ? kStarOn : kStarOff),
                                  0);
        lv_obj_set_style_transform_scale(_dots[i], 256, 0);
    }
    showWallet(wallet_after.stars, wallet_after.dust);
}

void ResultPage::showUnlock(math::Mode mode)
{
    if (_unlock == nullptr) {
        return;
    }
    lv_label_set_text(_unlock_name, math::modeKidName(mode));
    lv_obj_align(_unlock_name, LV_ALIGN_CENTER, 0, 80);
    lv_obj_clear_flag(_unlock, LV_OBJ_FLAG_HIDDEN);
}

void ResultPage::hideUnlock()
{
    if (_unlock != nullptr) {
        lv_obj_add_flag(_unlock, LV_OBJ_FLAG_HIDDEN);
    }
}

void ResultPage::showLevel(math::Level level)
{
    if (_level == nullptr) {
        return;
    }
    lv_label_set_text(_level, math::levelKidName(level));
    lv_obj_align(_level, LV_ALIGN_CENTER, 0, kLevelY);
}

void ResultPage::handleClicked()
{
    // Same rule as the quiz page: the click handler runs under the LVGL lock,
    // so it only records the tap.
    _tap_pending = true;
}

bool ResultPage::takeTap()
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    return true;
}

void ResultPage::clearTap()
{
    _tap_pending = false;
}
