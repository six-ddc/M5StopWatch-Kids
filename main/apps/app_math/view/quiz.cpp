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

constexpr const char* kTag = "QuizPage";

// Geometry, all relative to the centre of the 466 px round panel (radius 233).
//
// The two buttons sit on the bezel like ears, at roughly 10 and 2 o'clock
// (about +/-41 degrees off top). The answer cards are placed directly under
// them rather than along the bottom, so "press the left ear" and "pick the
// left card" are the same gesture in space and not just in name -- which is
// the whole reason a two-choice quiz suits this hardware.
//
// Card corners land 209 px from centre against the 233 px visible radius;
// tools/math_host_test/sim asserts that automatically.
constexpr int16_t kArcSize      = 440;  // outer edge lands at 225, inside 233
constexpr int16_t kArcWidth     = 10;
constexpr int16_t kCardW        = 150;
constexpr int16_t kCardH        = 92;
constexpr int16_t kCardX        = 88;   // +/- from centre, under the buttons
constexpr int16_t kCardY        = -95;
// The equation keeps the visual centre even though it is read second: at 96 px
// against the cards' 64 px it still lands first. Pushing it further down to
// preserve top-to-bottom reading order would leave the bottom of the circle
// empty and the whole layout top-heavy.
constexpr int16_t kEquationY    = 30;
constexpr int16_t kStatusY      = 150;  // least important line, so it goes last

// Pure black paper: on this AMOLED an unlit pixel emits nothing at all, so
// keeping the field black is the single biggest thing we do for young eyes.
constexpr uint32_t kInk         = 0xF2F0EA;
constexpr uint32_t kDim         = 0x8A8A88;
constexpr uint32_t kCardBg      = 0x141414;
constexpr uint32_t kCardBorder  = 0x3C3C3C;
constexpr uint32_t kRightBorder = 0x4CC66A;
constexpr uint32_t kRightBg     = 0x0E2A14;
constexpr uint32_t kWrongBorder = 0xE0574B;
constexpr uint32_t kWrongBg     = 0x2A0E0E;
constexpr uint32_t kTrackColor  = 0x1A1A1A;

// The progress ring shifts colour with the streak. It is the only reward that
// is always on screen, and it costs no extra lit pixels in the middle.
constexpr uint32_t kArcCalm     = 0x3A6EA8;
constexpr uint32_t kArcWarm     = 0x4BD0C0;
constexpr uint32_t kArcHot      = 0xE8B84B;

uint32_t arcColorForStreak(uint8_t streak)
{
    return streak >= 5 ? kArcHot : (streak >= 3 ? kArcWarm : kArcCalm);
}

// One-shot glow on the ring when the streak crosses a colour: the indicator
// briefly thickens and relaxes. It grows inward (the arc's outer edge is fixed
// by its bounding box at r=220), so it cannot escape the glass. Bound to the
// arc object, so LVGL drops it automatically if the page is destroyed mid-way.
void arcPulseCb(void* var, int32_t value)
{
    lv_obj_set_style_arc_width(static_cast<lv_obj_t*>(var), value, LV_PART_INDICATOR);
}

void startArcPulse(lv_obj_t* arc)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arcPulseCb);
    lv_anim_set_values(&a, kArcWidth, kArcWidth + 5);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_playback_duration(&a, 130);
    lv_anim_start(&a);
}

void cardClickedCb(lv_event_t* e)
{
    auto* page = static_cast<QuizPage*>(lv_event_get_user_data(e));
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (page == nullptr || obj == nullptr) {
        return;
    }
    const auto index = static_cast<uintptr_t>(
        reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)));
    page->handleCardClicked(index == 0);
}

}  // namespace

QuizPage::~QuizPage()
{
    destroy();
}

bool QuizPage::create(lv_obj_t* parent)
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

    // Progress ring, hugging the bezel. Not interactive -- the knob and the
    // click flag both have to go or a stray touch would drag the value.
    _arc = lv_arc_create(_root);
    lv_obj_set_size(_arc, kArcSize, kArcSize);
    lv_obj_center(_arc);
    lv_obj_remove_style(_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_rotation(_arc, 270);  // start at twelve o'clock
    lv_arc_set_bg_angles(_arc, 0, 360);
    lv_arc_set_range(_arc, 0, 100);
    lv_arc_set_value(_arc, 0);
    lv_obj_set_style_arc_width(_arc, kArcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_arc, kArcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(kTrackColor), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_arc, lv_color_hex(kArcCalm), LV_PART_INDICATOR);

    _status = lv_label_create(_root);
    lv_obj_set_style_text_font(_status, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_status, lv_color_hex(kDim), 0);
    lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_status, "");
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, kStatusY);

    _equation = lv_label_create(_root);
    lv_obj_set_style_text_font(_equation, &lv_font_digit_96, 0);
    lv_obj_set_style_text_color(_equation, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(_equation, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_equation, "");
    lv_obj_align(_equation, LV_ALIGN_CENTER, 0, kEquationY);

    for (uint8_t i = 0; i < 2; ++i) {
        _cards[i] = lv_obj_create(_root);
        lv_obj_remove_style_all(_cards[i]);
        lv_obj_set_size(_cards[i], kCardW, kCardH);
        lv_obj_align(_cards[i], LV_ALIGN_CENTER, i == 0 ? -kCardX : kCardX, kCardY);
        lv_obj_clear_flag(_cards[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_cards[i], 18, 0);
        lv_obj_set_style_bg_opa(_cards[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(_cards[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_cards[i], cardClickedCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(_cards[i], reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        _values[i] = lv_label_create(_cards[i]);
        lv_obj_set_style_text_font(_values[i], &lv_font_digit_64, 0);
        lv_obj_set_style_text_color(_values[i], lv_color_hex(kInk), 0);
        lv_obj_center(_values[i]);
        lv_label_set_text(_values[i], "");
    }
    resetCardStyles();

    return true;
}

void QuizPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_del(_root);
        _root = nullptr;
    }
    _arc      = nullptr;
    _status   = nullptr;
    _equation = nullptr;
    for (uint8_t i = 0; i < 2; ++i) {
        _cards[i]  = nullptr;
        _values[i] = nullptr;
    }
}

void QuizPage::setHidden(bool hidden)
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

void QuizPage::resetCardStyles()
{
    for (uint8_t i = 0; i < 2; ++i) {
        if (_cards[i] == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(_cards[i], lv_color_hex(kCardBg), 0);
        lv_obj_set_style_border_width(_cards[i], 3, 0);
        lv_obj_set_style_border_color(_cards[i], lv_color_hex(kCardBorder), 0);
        lv_obj_set_style_text_color(_values[i], lv_color_hex(kInk), 0);
    }
}

void QuizPage::showProblem(const math::Problem& problem, uint8_t index, uint8_t total,
                           uint8_t streak, bool in_retry, bool is_gold)
{
    if (_root == nullptr) {
        return;
    }
    _problem = problem;

    char buf[64];
    if (in_retry) {
        std::snprintf(buf, sizeof(buf), "错题 %u/%u", index, total);
    } else if (is_gold) {
        // The round's tenth question is the climax; the status line says so in
        // the ring's hottest colour.
        std::snprintf(buf, sizeof(buf), "%u/%u · 金星题", index, total);
    } else if (streak >= 2) {
        // A streak of one is just "the last answer was right"; not worth
        // putting on screen.
        std::snprintf(buf, sizeof(buf), "%u/%u · 连对 %u", index, total, streak);
    } else {
        std::snprintf(buf, sizeof(buf), "%u/%u", index, total);
    }
    lv_label_set_text(_status, buf);
    lv_obj_set_style_text_color(_status, lv_color_hex(is_gold ? kArcHot : kDim), 0);
    lv_obj_align(_status, LV_ALIGN_CENTER, 0, kStatusY);

    if (problem.kind == math::Kind::Judge) {
        // The completed equation cannot fit the 96 px face ("50 + 40 = 90"
        // measures ~600 px against a 466 px panel), so judge questions drop to
        // the 64 px face. That is fine: the reading task here is a whole
        // sentence, not a glance at two operands.
        std::snprintf(buf, sizeof(buf), "%u %c %u = %u", problem.lhs,
                      problem.op == math::Op::Add ? '+' : '-', problem.rhs,
                      problem.shownValue());
        lv_obj_set_style_text_font(_equation, &lv_font_digit_64, 0);
        lv_label_set_text(_values[0], "对");
        lv_label_set_text(_values[1], "错");
    } else {
        std::snprintf(buf, sizeof(buf), "%u %c %u", problem.lhs,
                      problem.op == math::Op::Add ? '+' : '-', problem.rhs);
        lv_obj_set_style_text_font(_equation, &lv_font_digit_96, 0);
        char value[8];
        std::snprintf(value, sizeof(value), "%u", problem.leftValue());
        lv_label_set_text(_values[0], value);
        std::snprintf(value, sizeof(value), "%u", problem.rightValue());
        lv_label_set_text(_values[1], value);
    }
    lv_label_set_text(_equation, buf);
    lv_obj_set_style_text_color(_equation, lv_color_hex(kInk), 0);
    lv_obj_align(_equation, LV_ALIGN_CENTER, 0, kEquationY);

    lv_obj_set_style_arc_color(_arc, lv_color_hex(arcColorForStreak(streak)), LV_PART_INDICATOR);
    lv_arc_set_value(_arc, total == 0 ? 0 : (index * 100) / total);

    resetCardStyles();
}

void QuizPage::showFeedback(bool correct, bool picked_left, uint8_t streak_after, bool milestone)
{
    if (_root == nullptr) {
        return;
    }

    const uint8_t picked  = picked_left ? 0 : 1;
    const uint8_t correct_index = _problem.answer_on_left ? 0 : 1;

    lv_obj_set_style_bg_color(_cards[picked], lv_color_hex(correct ? kRightBg : kWrongBg), 0);
    lv_obj_set_style_border_width(_cards[picked], 5, 0);
    lv_obj_set_style_border_color(_cards[picked],
                                  lv_color_hex(correct ? kRightBorder : kWrongBorder), 0);

    if (!correct) {
        // Outline the right one as well. The point of the pause is that the
        // child gets to see the answer they should have picked.
        lv_obj_set_style_bg_color(_cards[correct_index], lv_color_hex(kRightBg), 0);
        lv_obj_set_style_border_width(_cards[correct_index], 5, 0);
        lv_obj_set_style_border_color(_cards[correct_index], lv_color_hex(kRightBorder), 0);
    }

    if (_problem.kind == math::Kind::Judge) {
        // Whatever was answered, the pause ends on the true equation in green.
        // When the shown value was a lie this is the correction itself -- the
        // one line the child needs to read before the next question.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u %c %u = %u", _problem.lhs,
                      _problem.op == math::Op::Add ? '+' : '-', _problem.rhs, _problem.answer);
        lv_label_set_text(_equation, buf);
        lv_obj_set_style_text_color(_equation, lv_color_hex(kRightBorder), 0);
        lv_obj_align(_equation, LV_ALIGN_CENTER, 0, kEquationY);
    } else {
        // The equation text deliberately stays as it was. Appending " = 87"
        // would push the widest case to 436 px against 398 px of usable width
        // at this height -- and it would be saying twice what the green card
        // already says.
        lv_obj_set_style_text_color(_equation, lv_color_hex(correct ? kRightBorder : kInk), 0);
    }

    if (correct && milestone) {
        // The colour change used to happen silently between questions; moving
        // it here makes crossing the milestone an event the child actually
        // sees, with a single pulse to mark it.
        lv_obj_set_style_arc_color(_arc, lv_color_hex(arcColorForStreak(streak_after)),
                                   LV_PART_INDICATOR);
        startArcPulse(_arc);
    }
}

void QuizPage::handleCardClicked(bool left)
{
    // Runs inside lv_timer_handler with the LVGL lock already held. Do nothing
    // here but note it down; the app drains this from onRunning, where locking
    // and NVS are legal.
    _tap_pending = true;
    _tap_left    = left;
}

bool QuizPage::takeTap(bool& picked_left)
{
    if (!_tap_pending) {
        return false;
    }
    _tap_pending = false;
    picked_left  = _tap_left;
    return true;
}

void QuizPage::clearTap()
{
    _tap_pending = false;
}
