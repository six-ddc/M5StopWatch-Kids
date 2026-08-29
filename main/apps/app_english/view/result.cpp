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

using namespace eng_view;

namespace {

constexpr const char* kTag = "EngResultPage";

// Button hints go under the buttons -- ears at 10 and 2 o'clock -- at the same
// (+/-95, -145) the arithmetic app uses, not on a shared line along the bottom.
// Everything else drops 12 px to fill the space that leaves and to keep clear
// of the hint row, which runs down to y=-130.
constexpr int16_t kDotsY    = -98;
constexpr int16_t kScoreY   = -23;
constexpr int16_t kVerdictY = 52;
constexpr int16_t kMissedY  = 107;
constexpr int16_t kHintY    = -145;
constexpr int16_t kHintX    = 95;
constexpr int16_t kDotSize  = 16;
constexpr int16_t kDotGap   = 34;

void clickedCb(lv_event_t* e)
{
    auto* page = static_cast<ResultPage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->handleClicked();
    }
}

// One-shot swell as a star lands. Bound to the dot, so LVGL drops the
// animation if the page goes away mid-celebration.
void starPulseCb(void* var, int32_t value)
{
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(var), value, 0);
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
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, clickedCb, LV_EVENT_CLICKED, this);

    for (uint8_t d = 0; d < 3; ++d) {
        _dots[d] = lv_obj_create(_root);
        lv_obj_remove_style_all(_dots[d]);
        lv_obj_set_size(_dots[d], kDotSize, kDotSize);
        lv_obj_set_style_radius(_dots[d], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(_dots[d], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(_dots[d], lv_color_hex(kCardBorder), 0);
        lv_obj_align(_dots[d], LV_ALIGN_CENTER, static_cast<int16_t>((d - 1) * kDotGap), kDotsY);
    }

    // "8/10" -- the pinyin face is the only large one carrying both digits and
    // a slash, which saves generating a font for one line of text.
    _score = lv_label_create(_root);
    lv_obj_set_style_text_font(_score, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_score, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_score, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_score, "");
    lv_obj_align(_score, LV_ALIGN_CENTER, 0, kScoreY);

    _verdict = lv_label_create(_root);
    lv_obj_set_style_text_font(_verdict, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_verdict, lv_color_hex(kAccent), 0);
    lv_obj_set_style_text_align(_verdict, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_verdict, "");
    lv_obj_align(_verdict, LV_ALIGN_CENTER, 0, kVerdictY);

    // The words that went wrong, named rather than counted. This is the only
    // part of the page that is actually useful to a parent.
    _missed = lv_label_create(_root);
    lv_obj_set_style_text_font(_missed, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_missed, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_missed, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_missed, "");
    lv_obj_align(_missed, LV_ALIGN_CENTER, 0, kMissedY);

    _hint_a = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_a, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_a, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_a, "A 再来一次");
    lv_obj_align(_hint_a, LV_ALIGN_CENTER, -kHintX, kHintY);

    _hint_b = lv_label_create(_root);
    lv_obj_set_style_text_font(_hint_b, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_hint_b, lv_color_hex(kDim), 0);
    lv_label_set_text(_hint_b, "B 换一组");
    lv_obj_align(_hint_b, LV_ALIGN_CENTER, kHintX, kHintY);

    return true;
}

void ResultPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    for (uint8_t d = 0; d < 3; ++d) {
        _dots[d] = nullptr;
    }
    _score   = nullptr;
    _verdict = nullptr;
    _missed  = nullptr;
    _hint_a  = nullptr;
    _hint_b  = nullptr;
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

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u/%u", summary.correct, summary.total);
    lv_label_set_text(_score, buf);
    lv_obj_align(_score, LV_ALIGN_CENTER, 0, kScoreY);

    lv_label_set_text(_verdict, summary.verdict != nullptr ? summary.verdict : "");
    lv_obj_align(_verdict, LV_ALIGN_CENTER, 0, kVerdictY);

    if (summary.missed_count == 0) {
        lv_label_set_text(_missed, "");
    } else {
        // "需复习 cat dog" -- at most three, or the line runs off the glass.
        char line[64];
        int n = std::snprintf(line, sizeof(line), "需复习");
        for (uint8_t i = 0; i < summary.missed_count && i < 3; ++i) {
            if (summary.missed[i] == nullptr) {
                continue;
            }
            n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " %s", summary.missed[i]);
            if (n < 0 || static_cast<size_t>(n) >= sizeof(line)) {
                break;
            }
        }
        lv_label_set_text(_missed, line);
    }
    lv_obj_align(_missed, LV_ALIGN_CENTER, 0, kMissedY);

    for (uint8_t d = 0; d < 3; ++d) {
        lv_obj_set_style_bg_color(_dots[d], lv_color_hex(kCardBorder), 0);
        lv_obj_set_style_transform_scale(_dots[d], 256, 0);  // 256 == 1.0x
    }
}

void ResultPage::revealStar(uint8_t index)
{
    if (_root == nullptr || index >= 3 || _dots[index] == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(_dots[index], lv_color_hex(kAccent), 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _dots[index]);
    lv_anim_set_exec_cb(&a, starPulseCb);
    lv_anim_set_values(&a, 256, 400);
    lv_anim_set_duration(&a, 130);
    lv_anim_set_playback_duration(&a, 150);
    lv_anim_start(&a);
}

void ResultPage::finishSummary(const Summary& summary)
{
    if (_root == nullptr) {
        return;
    }
    for (uint8_t d = 0; d < 3; ++d) {
        lv_obj_set_style_bg_color(_dots[d], lv_color_hex(d < summary.stars ? kAccent : kCardBorder), 0);
        lv_obj_set_style_transform_scale(_dots[d], 256, 0);
    }
}

void ResultPage::handleClicked()
{
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
